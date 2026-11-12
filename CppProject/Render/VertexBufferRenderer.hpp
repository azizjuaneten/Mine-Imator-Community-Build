#pragma once
#include "Asset/Shader.hpp"
#include "Asset/VertexBuffer.hpp"
#include "Type/MatrixType.hpp"

namespace CppProject
{
	struct VertexBuffer;

	// Stores a batch of vertex buffers and submits them to the GPU when needed.
	// When possible, batches from the same or previous frame are re-used and combined.
	struct VertexBufferRenderer
	{
		VertexBufferRenderer();
		~VertexBufferRenderer();

		// Sets the optional occlusion-culling stage (off, occluder, query, cull).
		void SetOcclusionStage(IntType stage);

		// Adds an object to the queue with the current uniform settings.
		void Add(VertexBuffer* buffer);

		// Submits all the queued objects for rendering.
		void SubmitBatch();

		// Cleans up unused batches for the current frame.
		void EndFrame();

		// A list of objects to be rendered.
		struct Batch
		{
			~Batch();

			// Returns whether the batch matches another.
			bool operator==(const Batch& other) const;

			// Reset the batch for continued usage.
			void Reset();

			// Creates and binds vertex and index buffers.
			void CreateBuffers();

			IntType objects[MAX_BATCH_OBJECTS];
			Matrix matrixM[MAX_BATCH_OBJECTS];
			IntType numObjects = 0;
			Mesh<> mesh;
			Bounds bounds;
			bool isInCurrentFrame = false;
		};

		Batch* currentBatch = nullptr;
		Batch* submitBatch = nullptr;

		QVector<Batch*> activeBatches;

		IntType renderCalls = 0;
		IntType batchesCreated = 0;
		IntType batchesCopied = 0;
		IntType trianglesSubmitted = 0;

		struct OcclusionQuery
		{
		#if API_D3D11
			ID3D11Query* query = nullptr;
		#else
			GLuint query = 0;
		#endif
		};

		QHash<quint64, OcclusionQuery*> occlusionQueries;
		QHash<quint64, BoolType> occlusionResults;
		QVector<quint64> occlusionPending;
		QSet<quint64> occlusionUsed;
		IntType occlusionStage = 0;
		BoolType depthWriteBeforeQueries = false;

		quint64 GetOcclusionKey(const VertexBuffer* buffer) const;
		OcclusionQuery* GetOcclusionQuery(quint64 key);
		void ResolveOcclusionQueries();
	};
}