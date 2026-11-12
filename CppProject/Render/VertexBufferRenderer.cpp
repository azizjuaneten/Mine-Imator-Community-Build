#include "VertexBufferRenderer.hpp"

#include "AppHandler.hpp"
#include "Asset/Shader.hpp"
#include "Asset/VertexBuffer.hpp"
#include "GraphicsApiHandler.hpp"

#include <QThread>
#include <cstring>

namespace CppProject
{
	VertexBufferRenderer::VertexBufferRenderer()
	{
		currentBatch = new Batch;
	}

	VertexBufferRenderer::~VertexBufferRenderer()
	{
		for (OcclusionQuery* query : occlusionQueries)
		{
		#if API_D3D11
			releaseAndReset(query->query);
		#else
			if (query->query && GFX->glContext && QOpenGLContext::currentContext() == GFX->glContext)
				GFX->glDeleteQueries(1, &query->query);
		#endif
			delete query;
		}
	}

	quint64 VertexBufferRenderer::GetOcclusionKey(const VertexBuffer* buffer) const
	{
		quint64 hash = 1469598103934665603ull;
		auto mix = [&hash](const void* data, size_t size)
		{
			const uchar* bytes = static_cast<const uchar*>(data);
			for (size_t i = 0; i < size; i++)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
		};

		mix(&buffer->id, sizeof(buffer->id));
		mix(GFX->matrixM.m, sizeof(GFX->matrixM.m));
		IntType texture = -1;
		if (GFX->shader && GFX->shader->samplerState.size())
			texture = GFX->shader->samplerState[0].currentTexId;
		mix(&texture, sizeof(texture));
		return hash;
	}

	VertexBufferRenderer::OcclusionQuery* VertexBufferRenderer::GetOcclusionQuery(quint64 key)
	{
		if (OcclusionQuery* existing = occlusionQueries.value(key, nullptr))
			return existing;

		OcclusionQuery* query = new OcclusionQuery;
	#if API_D3D11
		D3D11_QUERY_DESC desc = {};
		desc.Query = D3D11_QUERY_OCCLUSION;
		if (FAILED(D3DDevice->CreateQuery(&desc, &query->query)))
		{
			delete query;
			return nullptr;
		}
	#else
		GFX->glGenQueries(1, &query->query);
		if (!query->query)
		{
			delete query;
			return nullptr;
		}
	#endif
		occlusionQueries.insert(key, query);
		return query;
	}

	void VertexBufferRenderer::SetOcclusionStage(IntType stage)
	{
		// Stage values: 0 off, 1 opaque depth, 2 query, 3 consume query results.
		if (stage == occlusionStage)
			return;

		GFX->SubmitBatch();
		if (stage == 1)
		{
			occlusionResults.clear();
			occlusionUsed.clear();
			GFX->SetDepthTest(true);
			GFX->SetDepthWrite(true);
		}
		else if (stage == 2)
		{
			occlusionPending.clear();
			occlusionUsed.clear();
			depthWriteBeforeQueries = GFX->depthMask;
			GFX->SetDepthTest(true);
			GFX->SetDepthWrite(false);
		}
		else if (stage == 3)
		{
			ResolveOcclusionQueries();
			GFX->SetDepthWrite(depthWriteBeforeQueries);
		}
		else if (occlusionStage == 2)
		{
			ResolveOcclusionQueries();
			GFX->SetDepthWrite(depthWriteBeforeQueries);
		}

		occlusionStage = stage;
	}

	void VertexBufferRenderer::ResolveOcclusionQueries()
	{
		for (quint64 key : occlusionPending)
		{
			OcclusionQuery* query = occlusionQueries.value(key, nullptr);
			if (!query)
				continue;

			BoolType visible = true;
		#if API_D3D11
			UINT64 samples = 1;
			HRESULT result;
			do
			{
				result = D3DContext->GetData(query->query, &samples, sizeof(samples), 0);
				if (result == S_FALSE)
					QThread::yieldCurrentThread();
			} while (result == S_FALSE);
			if (SUCCEEDED(result))
				visible = samples > 0;
			else
				D3DCheckError(result);
		#else
			GLuint samples = 1;
			GFX->glGetQueryObjectuiv(query->query, GL_QUERY_RESULT, &samples);
			visible = samples > 0;
			GL_CHECK_ERROR();
		#endif
			occlusionResults.insert(key, visible);
		}

		occlusionPending.clear();

		// Keep query resources bounded as objects and transforms change.
		for (auto it = occlusionQueries.begin(); it != occlusionQueries.end();)
		{
			if (!occlusionUsed.contains(it.key()))
			{
			#if API_D3D11
				releaseAndReset(it.value()->query);
			#else
				if (it.value()->query)
					GFX->glDeleteQueries(1, &it.value()->query);
			#endif
				delete it.value();
				it = occlusionQueries.erase(it);
			}
			else
				++it;
		}
	}

	void VertexBufferRenderer::Add(VertexBuffer* buffer)
	{
		// Empty
		if (buffer->meshes.size() == 0)
			return;

		Mesh<>* mesh = buffer->meshes[0];

		if (occlusionStage == 2)
		{
			GFX->SubmitBatch();
			const quint64 key = GetOcclusionKey(buffer);
			occlusionUsed.insert(key);

			// The same mesh/transform can be queried only once: depth writes are
			// disabled during this pass, so duplicate queries would be identical.
			if (occlusionResults.contains(key) || occlusionPending.contains(key))
				return;

			OcclusionQuery* query = GetOcclusionQuery(key);
			if (!query)
				return;
		#if API_D3D11
			D3DContext->Begin(query->query);
			buffer->Submit(GFX->shader, GFX->matrixM);
			D3DContext->End(query->query);
		#else
			GFX->glBeginQuery(GL_SAMPLES_PASSED, query->query);
			buffer->Submit(GFX->shader, GFX->matrixM);
			GFX->glEndQuery(GL_SAMPLES_PASSED);
			GL_CHECK_ERROR();
		#endif
			occlusionPending.append(key);
			return;
		}

		if (occlusionStage == 3)
		{
			const quint64 key = GetOcclusionKey(buffer);
			if (occlusionResults.contains(key) && !occlusionResults.value(key))
				return;
		}

		// Cull single-mesh objects before adding them to a shared batch.
		// Testing only the merged batch bounds keeps off-screen members alive
		// whenever another member intersects the camera frustum.
		if (buffer->meshes.size() == 1 && !mesh->bounds.empty)
		{
			Bounds worldBounds;
			worldBounds.AddBounds(mesh->bounds, GFX->matrixM);
			if (!GFX->IsVisible(worldBounds))
				return;
		}

		// Max triangles exceeded
		if (currentBatch->numObjects && currentBatch->mesh.numIndices + mesh->numIndices >= MAX_BATCH_INDICES)
			SubmitBatch();

		// Append new object
		currentBatch->objects[currentBatch->numObjects] = buffer->id;
		currentBatch->matrixM[currentBatch->numObjects] = GFX->matrixM;
		currentBatch->bounds.AddBounds(mesh->bounds, GFX->matrixM);
		currentBatch->mesh.numVertices += mesh->numVertices;
		currentBatch->mesh.numIndices += mesh->numIndices;
		currentBatch->numObjects++;

		bool submit = GFX->shader->SubmitObject();
		if (!GFX->shader->useBatching ||
			submit ||
			currentBatch->numObjects >= MAX_BATCH_OBJECTS ||
			currentBatch->mesh.numIndices >= MAX_BATCH_INDICES)
			SubmitBatch();
	}

	void VertexBufferRenderer::SubmitBatch()
	{
		if (!currentBatch->numObjects || !GFX->shader || !GFX->shader->IsLoaded())
			return;

		submitBatch = nullptr;

		// Bind the only submitted vertex buffer if batching is disabled or max triangles was exceeded
		if (currentBatch->numObjects == 1)
		{
			VertexBuffer* buffer = FindVertexBuffer(currentBatch->objects[0]);
			IntType calls = buffer->Submit(GFX->shader, currentBatch->matrixM[0]);
			renderCalls += calls;
			if (calls)
				trianglesSubmitted += buffer->numIndices / 3;
		}
		else
		{
			// Look for identical batches
			for (Batch* frameBatch : activeBatches)
			{
				if (*currentBatch == *frameBatch)
				{
					submitBatch = frameBatch;
					break;
				}
			}

			// Use the current batch
			if (!submitBatch)
			{
				submitBatch = currentBatch;
				submitBatch->CreateBuffers();
				activeBatches.append(submitBatch);
			}

			// Render if within view
			if (GFX->IsVisible(currentBatch->bounds))
			{
				submitBatch->mesh.BeginUse();
				GFX->shader->SubmitVertices(Shader::TRIANGLE_LIST, submitBatch->mesh.numIndices);
				submitBatch->mesh.EndUse();
				renderCalls++;
				trianglesSubmitted += submitBatch->mesh.numIndices / 3;
			}
			else
				GFX->shader->ResetObjects();
		}

		// Mark as active
		if (submitBatch)
			submitBatch->isInCurrentFrame = true;

		// Reset current batch
		if (submitBatch == currentBatch)
			currentBatch = new Batch;
		else
			currentBatch->Reset();
	}

	void VertexBufferRenderer::EndFrame()
	{
		SubmitBatch();

		// Free unused batches
		IntType batchesFreed = 0;
		for (IntType b = 0; b < activeBatches.size(); b++)
		{
			Batch* batch = activeBatches[b];
			if (!batch->isInCurrentFrame)
			{
				delete batch;
				batchesFreed++;
				activeBatches.removeAt(b--);
			}
			else
				batch->isInCurrentFrame = false;
		}

		if (batchesCreated)
			batchesCreated = 0;

		if (batchesCopied)
			batchesCopied = 0;
	}

	VertexBufferRenderer::Batch::~Batch()
	{
		mesh.FreeBuffers();
	}

	bool VertexBufferRenderer::Batch::operator==(const Batch& other) const
	{
		if (numObjects != other.numObjects ||
			mesh.numVertices != other.mesh.numVertices ||
			mesh.numIndices != other.mesh.numIndices)
			return false;

		for (IntType i = 0; i < numObjects; i++)
			if (objects[i] != other.objects[i])
				return false;

		return true;
	}

	void VertexBufferRenderer::Batch::Reset()
	{
		numObjects = 0;
		bounds.Reset();
		mesh.numVertices = mesh.numIndices = 0;
		mesh.FreeBuffers();
	}

	void VertexBufferRenderer::Batch::CreateBuffers()
	{
		mesh.vertexData.Alloc(mesh.numVertices);
		mesh.indexData.Alloc(mesh.numIndices);

		IntType vertexOffset = 0, indexOffset = 0;
		for (IntType objIndex = 0; objIndex < numObjects; objIndex++)
		{
			Mesh<>* objMesh = FindVertexBuffer(objects[objIndex])->meshes[0];
			if (!objMesh->vertexData.Size())
				continue;

			// Copy vertices and assign object index
			memcpy(&mesh.vertexData[vertexOffset], objMesh->vertexData.Data(), objMesh->numVertices * sizeof(Vertex));
			for (IntType v = 0; v < objMesh->numVertices; v++)
				mesh.vertexData[vertexOffset + v].SetIndex(objIndex);

			// Copy indices and apply vertex offset
			for (IntType i = 0; i < objMesh->numIndices; i++)
				mesh.indexData[indexOffset + i] = objMesh->indexData.Value(i) + vertexOffset;

			vertexOffset += objMesh->numVertices;
			indexOffset += objMesh->numIndices;
		}

		mesh.CreateBuffers();
		VB->batchesCreated++;
	}
}