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
		if (toPage)
		{
			// Check if image is supported for a texture page
			if (pageLoc = TexturePage::Add(image))
				this->image = QImage();
		}
	}

	BoolType Sprite::Frame::IsOpaque() const
	{
		if (opacityChecked)
			return opaque;

		opacityChecked = true;
		opaque = false;

		const QImage* source = &image;
		QRect sourceRect(QPoint(0, 0), image.size());
		if (pageLoc && pageLoc->page)
		{
			source = &pageLoc->page->image;
			sourceRect = pageLoc->rect;
		}

		if (source->isNull())
			return opaque;

		opaque = true;
		if (!source->hasAlphaChannel())
			return opaque;

		// Below 75% opacity, the texture is treated as transparent for this prototype.
		constexpr uchar MIN_OCCLUDER_ALPHA = 192;
		const QImage::Format format = source->format();
		if (format == QImage::Format_RGBA8888 || format == QImage::Format_RGBA8888_Premultiplied)
		{
			for (IntType y = 0; y < sourceRect.height() && opaque; y++)
			{
				const uchar* row = source->constScanLine(sourceRect.y() + (int)y) + sourceRect.x() * 4;
				for (IntType x = 0; x < sourceRect.width(); x++)
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
			for (IntType y = 0; y < sourceRect.height() && opaque; y++)
				for (IntType x = 0; x < sourceRect.width(); x++)
					if (qAlpha(source->pixel(sourceRect.x() + (int)x, sourceRect.y() + (int)y)) < MIN_OCCLUDER_ALPHA)
					{
						opaque = false;
						break;
					}
		}

		return opaque;
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