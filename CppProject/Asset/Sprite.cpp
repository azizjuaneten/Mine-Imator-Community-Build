#include "Sprite.hpp"

#include "Render/Texture.hpp"
#include "Render/TexturePage.hpp"
#include "Shader.hpp"
#include "Surface.hpp"

namespace CppProject
{
	QVector<Sprite*> Sprite::allSprites;

	Sprite::Sprite(Sprite* other) : Asset(ID_Sprite)
	{
		allSprites.append(this);
		size = other->size;
		origin = other->origin;
		
		for (Frame* frame : other->frames)
		{
			QImage copy;
			if (frame->pageLoc)
				copy = frame->pageLoc->page->image.copy(frame->pageLoc->rect);
			else
				copy = frame->image.copy();

			
			frames.append(new Frame(copy, false));
		}
	}

	Sprite::Sprite(QString name, IntType subAssetId, int numFrames, QPoint origin) : Asset(ID_Sprite, subAssetId, name)
	{
		allSprites.append(this);
		this->origin = origin;

		for (int f = 0; f < numFrames; f++)
		{
			QImage image(":/Sprites/" + name + "_frame_" + NumStr(f) + ".png");
			image.convertTo(QImage::Format_RGBA8888);
			size = image.size();
			frames.append(new Frame(image, true));
		}
	}

	Sprite::Sprite(const StringType& filename, QPoint origin) : Asset(ID_Sprite)
	{
		allSprites.append(this);
		this->origin = origin;

		QImage image(filename);
		image.convertTo(QImage::Format_RGBA8888);
		size = image.size();
		frames.append(new Frame(image, false));
	}

	Sprite::Sprite(Surface* surface, QPoint origin) : Asset(ID_Sprite)
	{
		allSprites.append(this);
		useTexturePage = false;
		this->origin = origin;
		size = surface->size;

		frames.append(new Frame(surface->ToImage(), false));
	}
	
	Sprite::Sprite(Surface* surface, QRect rect, QPoint origin) : Asset(ID_Sprite)
	{
		allSprites.append(this);
		useTexturePage = false;
		this->origin = origin;
		size = surface->size;

		frames.append(new Frame(surface->ToImage().copy(rect), false));
	}

	Sprite::~Sprite()
	{
		allSprites.removeOne(this);

		for (Frame* frame : frames)
			delete frame;
	}

	BoolType Sprite::IsOpaque() const
	{
		if (frames.isEmpty())
			return false;

		for (const Frame* frame : frames)
			if (!frame->IsOpaque())
				return false;

		return true;
	}

	IntType Sprite::GetTexture(IntType index)
	{
		if (index >= frames.size())
			return -1;

		if (index < 0)
			index = 0;

		return frames[index]->GetTexture();
	}

	QColor Sprite::GetPixel(IntType index, QPoint point) const
	{
		if (index < 0 || index >= frames.size())
			return Qt::transparent;

		Frame* frame = frames[index];
		if (frame->pageLoc) // Pixel from texture page
			return frame->pageLoc->page->image.pixelColor(frame->pageLoc->rect.topLeft() + point);
		else // Pixel from image
			return frame->image.pixelColor(point);
	}

	Sprite::Frame::Frame(QImage image, bool toPage) : image(image)
	{
		// Treat pixels below 75% opacity as transparent for conservative occluder classification.
		// Minor alpha variations from antialiased edges are ignored for this experiment.
		constexpr uchar MIN_OCCLUDER_ALPHA = 192;
		opaque = !image.isNull();
		if (opaque && image.hasAlphaChannel())
		{
			const QImage::Format format = image.format();
			if (format == QImage::Format_RGBA8888 || format == QImage::Format_RGBA8888_Premultiplied)
			{
				// The common RGBA formats store alpha in the final byte; scanning
				// scanlines avoids the per-pixel conversion performed by QImage::pixel().
				for (IntType y = 0; y < image.height() && opaque; y++)
				{
					const uchar* row = image.constScanLine((int)y);
					for (IntType x = 0; x < image.width(); x++)
						if (row[x * 4 + 3] < MIN_OCCLUDER_ALPHA)
						{
							opaque = false;
							break;
						}
				}
			}
			else
			{
				// Keep uncommon QImage formats correct, including indexed alpha.
				for (IntType y = 0; y < image.height() && opaque; y++)
					for (IntType x = 0; x < image.width(); x++)
						if (qAlpha(image.pixel((int)x, (int)y)) < MIN_OCCLUDER_ALPHA)
						{
							opaque = false;
							break;
						}
			}
		}

		if (toPage)
		{
			// Check if image is supported for a texture page
			if (pageLoc = TexturePage::Add(image))
				this->image = QImage();
		}
	}

	Sprite::Frame::~Frame()
	{
		deleteAndReset(texture);
		deleteAndReset(pageLoc);
	}

	IntType Sprite::Frame::GetTexture()
	{
		// Get texture page location ID or texture ID from QImage
		if (pageLoc)
			return pageLoc->id;

		else if (!image.isNull())
		{
			if (!texture)
				texture = new Texture(image);
			return texture->GetId();
		}

		return -1;
	}

	bool Sprite::Frame::MoveToTexturePage()
	{
		if (pageLoc) // Already on texture page
			return false;

		if (pageLoc = TexturePage::Add(image))
		{
			deleteAndReset(texture);
			image = QImage(); // Free image
		}

		return true;
	}
}