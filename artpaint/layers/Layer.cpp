/*
 * Copyright 2003, Heikki Suhonen
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		Heikki Suhonen <heikki.suhonen@gmail.com>
 *
 */

#include "Layer.h"

#include "BitmapUtilities.h"
#include "Image.h"
#include "ImageView.h"
#include "LayerView.h"
#include "LayerWindow.h"
#include "MessageConstants.h"
#include "PixelOperations.h"
#include "ProjectFileFunctions.h"
#include "Selection.h"
#include "SettingsServer.h"
#include "UtilityClasses.h"


#include <File.h>
#include <Window.h>


#include "zlib.h"
#include <new>
#include <stdio.h>


Layer::Layer(
	BRect frame, int32 id, ImageView* imageView, layer_type type, BBitmap* bitmap, BRect* offset)
	:
	fLayerData(NULL),
	fLayerPreview(NULL),
	fLayerId(id),
	fLayerPreviewSem(-1),
	fLayerPreviewThreads(0),
	fLayerVisible(true),
	fLayerActive(false),
	fLayerType(type),
	fImage(NULL),
	fImageView(imageView),
	fLayerView(NULL),
	fBlendMode(BLEND_NORMAL)
{
	frame.OffsetTo(B_ORIGIN);
	fLayerName << B_TRANSLATE("Layer") << " " << fLayerId;

	if (fLayerType == HS_NORMAL_LAYER) {
		BRect sourceRect;
		if (bitmap && bitmap->IsValid())
			sourceRect = bitmap->Bounds();

		fLayerData = new BBitmap(BRect(frame.LeftTop(),
			BPoint(max_c(frame.right, sourceRect.right),
			 max_c(frame.bottom, sourceRect.bottom))), B_RGBA32);

		if (!fLayerData->IsValid())
			throw std::bad_alloc();

		union color_conversion color;
		color.bytes[0] = 0xFF;
		color.bytes[1] = 0xFF;
		color.bytes[2] = 0xFF;
		color.bytes[3] = 0x00;

		int32 bits_length = fLayerData->BitsLength() / 4;
		uint32* target_bits = (uint32*)fLayerData->Bits();

		// Fill the layer data with initial color.
		for (int32 i = 0; i < bits_length; ++i)
			*target_bits++ = color.word;

		if (bitmap && bitmap->IsValid()) {
			uint32 bpr = fLayerData->BytesPerRow();
			uint32 bmp_offset = 0;
			if (offset != NULL)
				bmp_offset = (offset->left * 4) + (offset->top * bpr);

			fLayerData->ImportBits(
				bitmap->Bits(), bitmap->BitsLength(), bitmap->BytesPerRow(), bmp_offset, B_RGBA32);
		}

		// create the miniature image for this layer and a semaphore for it
		fLayerPreview = new BBitmap(
			BRect(0, 0, HS_MINIATURE_IMAGE_WIDTH - 1, HS_MINIATURE_IMAGE_HEIGHT - 1), B_RGB_32_BIT);
		fLayerPreviewSem = create_sem(1, "mini image semaphore");
	}

	fLayerView = new LayerView(fLayerPreview, this);

	SetTransparency(1.0);
}


Layer::~Layer()
{
	delete fLayerView;

	delete fLayerData;
	delete fLayerPreview;
}


void
Layer::AddToImage(Image* im)
{
	fImage = im;
}


void
Layer::SetTransparency(float coefficient, bool update_old)
{
	transparency_coefficient = coefficient;
	for (int i = 0; i < 256; ++i) {
		float a = (float)i / 255.0 * transparency_coefficient;
		float_alpha_table[i] = a;

		a *= 32768;
		fixed_alpha_table[i] = (uint32)a;
	}

	if (update_old == true)
		old_transparency_coefficient = transparency_coefficient;
}


void
Layer::Clear(rgb_color color)
{
	// This will copy the color (including alpha) to every pixel in this layer.
	// If the selection is not empty, the color will be copied only to the
	// selected points
	Selection* selection = fImageView->GetSelection();

	// we will copy the color to this in correct order
	uint32 color_bits = RGBColorToBGRA(color);

	uint32* bits = (uint32*)fLayerData->Bits();
	if (selection->IsEmpty()) {
		int32 bitslength = fLayerData->BitsLength() / 4;
		for (int32 i = 0; i < bitslength; ++i)
			*bits++ = color_bits;
	} else {
		BRect bounds = selection->GetBoundingRect();
		int32 left = (int32)bounds.left;
		int32 top = (int32)bounds.top;
		int32 right = (int32)bounds.right;
		int32 bottom = (int32)bounds.bottom;
		int32 bpr = fLayerData->BytesPerRow() / 4;
		for (int32 y = top; y <= bottom; ++y) {
			for (int32 x = left; x <= right; ++x) {
				if (selection->ContainsPoint(x, y)) {
					union color_conversion norm_color;
					norm_color.word = color_bits;
					norm_color.bytes[3] = selection->Value(x, y);

					*(bits + x + y * bpr) = dst_out_fixed(*(bits + x + y * bpr), norm_color.word);
				}
			}
		}
	}
}


int32
Layer::CreateMiniatureImage(void* data)
{
	if (Layer* layer = static_cast<Layer*>(data))
		return layer->calc_mini_image();
	return B_ERROR;
}


int32
Layer::calc_mini_image()
{
	// This function might crash if it is executing while the layer is
	// destroyed. This is because the bitmap and miniature image are destroyed.
	// We need something to stop this thread when the layer is being destroyed.

	int32 gridSize;
	uint32 color1;
	uint32 color2;

	gridSize = 20;
	rgb_color rgb1, rgb2;
	rgb1.red = rgb1.green = rgb1.blue = 0xBB;
	rgb2.red = rgb2.green = rgb2.blue = 0x99;
	rgb1.alpha = rgb2.alpha = 0xFF;
	color1 = RGBColorToBGRA(rgb1);
	color2 = RGBColorToBGRA(rgb2);

	if (SettingsServer* server = SettingsServer::Instance()) {
		BMessage settings;
		server->GetApplicationSettings(&settings);

		gridSize = settings.GetInt32(skBgGridSize, gridSize);
		color1 = settings.GetUInt32(skBgColor1, color1);
		color2 = settings.GetUInt32(skBgColor2, color2);
	}

	gridSize = max_c(gridSize / 5, 4);

	// increase the number of waiting threads
	atomic_add(&fLayerPreviewThreads, 1);
	// aquire the semaphore that is required to access the fLayerPreview
	acquire_sem(fLayerPreviewSem);
	// decrease the number of waiting threads
	atomic_add(&fLayerPreviewThreads, -1);

	int32 miniature_width = (int32)(HS_MINIATURE_IMAGE_WIDTH
		* (min_c(fLayerData->Bounds().Width() / fLayerData->Bounds().Height(), 1)));
	int32 miniature_height = (int32)(HS_MINIATURE_IMAGE_HEIGHT
		* (min_c(fLayerData->Bounds().Height() / fLayerData->Bounds().Width(), 1)));

	// Here we copy the contents of fLayerData to miniature image.
	// by using a DDA-scaling algorithm first take the dx and dy variables
	float dx = (fLayerData->Bounds().Width() + 1) / (float)miniature_width;
	float dy = (fLayerData->Bounds().Height() + 1) / (float)miniature_height;
	int32 x = 0, y = 0;

	int32 x_offset_left = (int32)floor((float)(HS_MINIATURE_IMAGE_WIDTH - miniature_width) / 2.0);
	int32 x_offset_right = (int32)ceil((float)(HS_MINIATURE_IMAGE_WIDTH - miniature_width) / 2.0);

	int32 y_offset = (HS_MINIATURE_IMAGE_HEIGHT - miniature_height) / 2;

	// The bitmap might be changed and deleted while we are accessing it.
	int32 b_bpr = fLayerData->BytesPerRow() / 4;
	uint32* big_image;

	BRect bounds = fLayerPreview->Bounds();
	BitmapUtilities::CheckerBitmap(fLayerPreview, color1, color2, gridSize, &bounds);

	union color_conversion color;

	uint32* small_image = (uint32*)fLayerPreview->Bits();
	big_image = (uint32*)fLayerData->Bits();
	// Clear the parts that we do not set.
	small_image += HS_MINIATURE_IMAGE_WIDTH * y_offset;

	while ((y < miniature_height) && (fLayerPreviewThreads == 0)) {
		small_image += x_offset_left;

		while ((x < miniature_width) && (fLayerPreviewThreads == 0)) {
			color.word = *(big_image + ((int32)(y * dy)) * b_bpr + (int32)(x * dx));

			*small_image = src_over_fixed(*small_image, color.word);
			small_image++;
			x++;
		}
		y++;

		small_image += x_offset_right;

		x = 0;
	}

	if (fLayerPreviewThreads == 0) {
		snooze(50 * 1000);
		if (fLayerPreviewThreads == 0) {
			if (fLayerView->LockLooper()) {
				fLayerView->UpdateImage();
				BView* bmap_view;
				if ((bmap_view = fLayerView->Window()->FindView("bitmap_view")) != NULL)
					bmap_view->Draw(bmap_view->Bounds());

				fLayerView->UnlockLooper();
			}
		}
	}
	release_sem(fLayerPreviewSem);

	return B_OK;
}


void
Layer::ChangeBitmap(BBitmap* newBitmap)
{
	delete fLayerData;
	fLayerData = newBitmap;
}


void
Layer::ActivateLayer(bool active)
{
	fLayerActive = active;
	fLayerView->Activate(active);
}


void
Layer::SetVisibility(bool visible)
{
	fLayerVisible = visible;
	fLayerView->SetVisibility(visible);
	if (fLayerView->LockLooper() == true) {
		fLayerView->Draw(fLayerView->Bounds());
		fLayerView->Invalidate();
		fLayerView->UnlockLooper();
	}
}


void
Layer::Merge(Layer* top_layer)
{
	int32 top_bpr = top_layer->Bitmap()->BytesPerRow() / 4;
	int32 bottom_bpr = Bitmap()->BytesPerRow() / 4;
	uint32* top_bits = (uint32*)top_layer->Bitmap()->Bits();
	uint32* bottom_bits = (uint32*)Bitmap()->Bits();

	int32 height = (int32)min_c(
		top_layer->Bitmap()->Bounds().Height() + 1, Bitmap()->Bounds().Height() + 1);
	int32 width = (int32)min_c(
		top_layer->Bitmap()->Bounds().Width() + 1, Bitmap()->Bounds().Width() + 1);

	union color_conversion top, bottom;

	float top_coefficient = top_layer->GetTransparency();
	float bot_coefficient = GetTransparency();

	for (int32 y = 0; y < height; ++y) {
		for (int32 x = 0; x < width; ++x) {
			top.word = *(top_bits + x + y * top_bpr);
			top.bytes[3] = top.bytes[3] * top_coefficient;
			bottom.word = *(bottom_bits + x + y * bottom_bpr);
			bottom.bytes[3] = bottom.bytes[3] * bot_coefficient;

			*(bottom_bits + x + y * bottom_bpr)
				= src_over_fixed_blend(bottom.word, top.word, top_layer->GetBlendMode());
		}
	}

	// Change the transparency to 1.0
	SetTransparency(1.0);
}


Layer*
Layer::readLayer(BFile& file, ImageView* imageView, int32 new_id, bool is_little_endian,
	int32 compression_method)
{
	// This is the new way of reading the layers.
	int32 marker;
	if (file.Read(&marker, sizeof(int32)) != sizeof(int32))
		return NULL;

	if (is_little_endian)
		marker = B_LENDIAN_TO_HOST_INT32(marker);
	else
		marker = B_BENDIAN_TO_HOST_INT32(marker);

	if (marker != PROJECT_FILE_LAYER_START_MARKER)
		return NULL;

	int32 width;
	int32 height;
	layer_type layerType;
	int32 layer_visibility;
	int64 length;
	if (file.Read(&width, sizeof(int32)) != sizeof(int32))
		return NULL;
	if (file.Read(&height, sizeof(int32)) != sizeof(int32))
		return NULL;
	if (file.Read(&layerType, sizeof(int32)) != sizeof(int32))
		return NULL;
	if (file.Read(&layer_visibility, sizeof(int32)) != sizeof(int32))
		return NULL;
	if (file.Read(&length, sizeof(int64)) != sizeof(int64))
		return NULL;

	if (is_little_endian) {
		width = B_LENDIAN_TO_HOST_INT32(width);
		height = B_LENDIAN_TO_HOST_INT32(height);
		layerType = layer_type(B_LENDIAN_TO_HOST_INT32(layerType));
		length = B_LENDIAN_TO_HOST_INT64(length);
	} else {
		width = B_BENDIAN_TO_HOST_INT32(width);
		height = B_BENDIAN_TO_HOST_INT32(height);
		layerType = layer_type(B_BENDIAN_TO_HOST_INT32(layerType));
		length = B_BENDIAN_TO_HOST_INT64(length);
	}

	Layer* layer = new Layer(BRect(0, 0, width - 1, height - 1), new_id, imageView, layerType);
	layer->SetVisibility((uint32(layer_visibility) == 0xFFFFFFFF));

	int8* bits = (int8*)layer->Bitmap()->Bits();

	uint8* compressedBits = NULL;

	uint64 old_length = length;

	if (compression_method == ZLIB_COMPRESSION) {
		compressedBits = (uint8*)malloc(length);

		if (file.Read(compressedBits, length) != length) {
			free(compressedBits);
			delete layer;
			return NULL;
		}
	} else {
		if (file.Read(bits, length) != length) {
			delete layer;
			return NULL;
		}
	}

	// Read the end-marker.
	if (file.Read(&marker, sizeof(int32)) != sizeof(int32)) {
		delete layer;
		return NULL;
	}
	if (is_little_endian)
		marker = B_LENDIAN_TO_HOST_INT32(marker);
	else
		marker = B_BENDIAN_TO_HOST_INT32(marker);

	if (marker != PROJECT_FILE_LAYER_END_MARKER) {
		delete layer;
		return NULL;
	}

	// Here try to read the extra-data block.
	if (file.Read(&marker, sizeof(int32)) == sizeof(int32)) {
		if (is_little_endian)
			marker = B_LENDIAN_TO_HOST_INT32(marker);
		else
			marker = B_BENDIAN_TO_HOST_INT32(marker);

		if (marker == PROJECT_FILE_LAYER_EXTRA_DATA_START_MARKER) {
			// Read the length of this section
			int32 length;
			if (file.Read(&length, sizeof(int32)) != sizeof(int32)) {
				delete layer;
				return NULL;
			}

			if (is_little_endian)
				length = B_LENDIAN_TO_HOST_INT32(length);
			else
				length = B_BENDIAN_TO_HOST_INT32(length);

			// Read the transparency coefficient
			float coeff;
			if (file.Read(&coeff, sizeof(float)) != sizeof(float)) {
				delete layer;
				return NULL;
			}
			if (is_little_endian)
				coeff = B_LENDIAN_TO_HOST_FLOAT(coeff);
			else
				coeff = B_BENDIAN_TO_HOST_FLOAT(coeff);

			layer->SetTransparency(coeff);
			length -= sizeof(float);

			if (length > sizeof(int32)) {
				int32 nameLen;
				if (file.Read(&nameLen, sizeof(int32)) == sizeof(int32)) {
					char name[nameLen + 1];
					length -= sizeof(int32);
					if (file.Read(&name, nameLen) == nameLen) {
						name[nameLen] = 0;
						layer->SetName(name);
						length -= nameLen;
					}
				}
			}

			if (length > 0) {
				uint8 blend_mode;
				if (file.Read(&blend_mode, sizeof(uint8)) == sizeof(uint8)) {
					length -= sizeof(uint8);
					layer->SetBlendMode(blend_mode);
				}
			}

			if (compression_method == ZLIB_COMPRESSION) {
				uint64 actual_length;
				if (file.Read(&actual_length, sizeof(int64)) != sizeof(int64))
					return NULL;

				uint8* uncompressedBits = (uint8*)malloc(actual_length);
				unsigned long uncompressedLength = static_cast<unsigned long>(actual_length);

				int z_result
					= uncompress(uncompressedBits, &uncompressedLength, compressedBits, old_length);

				free(compressedBits);
				if (z_result == Z_OK) {
					memcpy(bits, uncompressedBits, actual_length);
					free(uncompressedBits);
				} else {
					delete layer;
					free(uncompressedBits);
					return NULL;
				}

				length -= sizeof(int64);
			}

			file.Seek(length, SEEK_CUR);

			// Here we should get the end-marker for layer's extra data
			if (file.Read(&marker, sizeof(int32)) != sizeof(int32)) {
				delete layer;
				return NULL;
			}
			if (is_little_endian)
				marker = B_LENDIAN_TO_HOST_INT32(marker);
			else
				marker = B_BENDIAN_TO_HOST_INT32(marker);

			if (marker != PROJECT_FILE_LAYER_EXTRA_DATA_END_MARKER) {
				delete layer;
				return NULL;
			}
		} else {
			// Somehow -sizeof(int32) does not seem to work????
			file.Seek(-4, SEEK_CUR);
		}
	}

	// Before returning calculate the layer's miniature image.
	layer->calc_mini_image();

	return layer;
}


Layer*
Layer::readLayerOldStyle(BFile& file, ImageView* imageView, int32 new_id)
{
	// Layer has stored the following things:
	//	1.	Layer frame (i.e. the frame of bitmap)
	//	2.	Layer id
	//	3.	Layer type
	//	4.	Layer visibility
	//	5.	Bitmap data

	BRect layer_frame;
	if (file.Read(&layer_frame, sizeof(BRect)) != sizeof(BRect))
		return NULL;

	// The layer id is written to the file, so it must be read also, but it will
	// not be used. Instead we use the id that is provided as a parameter
	int32 id; // This is not actually used.
	if (file.Read(&id, sizeof(uint32)) != sizeof(uint32))
		return NULL;
	id = B_BENDIAN_TO_HOST_INT32(id);

	layer_type layerType;
	if ((file.Read(&layerType, sizeof(uint32)) != sizeof(uint32)))
		return NULL;

	bool visi;
	if (file.Read(&visi, sizeof(bool)) != sizeof(bool))
		return NULL;

	// Old files project-files are all big-endian so we convert data here.
	layer_frame.left = B_BENDIAN_TO_HOST_FLOAT(layer_frame.left);
	layer_frame.right = B_BENDIAN_TO_HOST_FLOAT(layer_frame.right);
	layer_frame.top = B_BENDIAN_TO_HOST_FLOAT(layer_frame.top);
	layer_frame.bottom = B_BENDIAN_TO_HOST_FLOAT(layer_frame.bottom);

	layerType = layer_type(B_BENDIAN_TO_HOST_INT32(layerType));
	if (layerType != HS_NORMAL_LAYER)
		return NULL;

	// Create the layer
	Layer* layer = new Layer(layer_frame, new_id, imageView, layerType);
	layer->SetVisibility(visi);

	int8* bits = (int8*)layer->Bitmap()->Bits();
	// align the file pointer to four-byte boundary.
	int32 alignment_offset;
	alignment_offset = (4 - (file.Position() % 4)) % 4;

	if (file.Read(bits, alignment_offset) != alignment_offset) {
		delete layer;
		return NULL;
	}
	bits += alignment_offset;

	if (file.Read(bits, layer->Bitmap()->BitsLength() - alignment_offset)
		!= (layer->Bitmap()->BitsLength() - alignment_offset)) {
		delete layer;
		return NULL;
	}
	// Before returning calculate the layer's miniature image.
	layer->calc_mini_image();
	return layer;
}


int64
Layer::writeLayer(BFile& file, int32 compression_method)
{
	int64 written_bytes = 0;
	int32 marker = PROJECT_FILE_LAYER_START_MARKER;
	int32 visi;
	if (fLayerVisible == TRUE)
		visi = 0xFFFFFFFF;
	else
		visi = 0x00000000;

	written_bytes += file.Write(&marker, sizeof(int32));
	int32 width = fLayerData->Bounds().IntegerWidth() + 1;
	int32 height = fLayerData->Bounds().IntegerHeight() + 1;

	written_bytes += file.Write(&width, sizeof(int32));
	written_bytes += file.Write(&height, sizeof(int32));
	written_bytes += file.Write(&fLayerType, sizeof(int32));
	written_bytes += file.Write(&visi, sizeof(int32));

	int64 data_length = fLayerData->BitsLength();
	int z_result = Z_OK;

	if (compression_method == ZLIB_COMPRESSION) {
		unsigned long dataLengthCompressedULong = (data_length * 1.1) + 12;

		uint8* dataCompressed = (uint8*)malloc(dataLengthCompressedULong);
		z_result = compress(
			dataCompressed,
			&dataLengthCompressedULong,
			(uint8*)fLayerData->Bits(),
			static_cast<unsigned long>(data_length));

		if (z_result == Z_OK) {
			int64 dataLengthCompressed = static_cast<int64>(dataLengthCompressedULong);

			written_bytes += file.Write(&dataLengthCompressed, sizeof(int64));
			written_bytes += file.Write(dataCompressed, dataLengthCompressed);
		} else {
			written_bytes += file.Write(&data_length, sizeof(int64));
			written_bytes += file.Write(fLayerData->Bits(), data_length);
		}

		free(dataCompressed);
	} else {
		written_bytes += file.Write(&data_length, sizeof(int64));
		written_bytes += file.Write(fLayerData->Bits(), data_length);
	}

	marker = PROJECT_FILE_LAYER_END_MARKER;
	written_bytes += file.Write(&marker, sizeof(int32));

	// Here we write the layer extra-data to the file.
	marker = PROJECT_FILE_LAYER_EXTRA_DATA_START_MARKER;
	written_bytes += file.Write(&marker, sizeof(int32));

	int32 nameLen = fLayerName.Length();
	// this is the length of the extra data
	marker = sizeof(float) + sizeof(int32) + nameLen + sizeof(uint8) + sizeof(int64);
	written_bytes += file.Write(&marker, sizeof(int32));
	written_bytes += file.Write(&transparency_coefficient, sizeof(float));
	written_bytes += file.Write(&nameLen, sizeof(int32));
	written_bytes += file.Write(fLayerName.String(), nameLen);

	uint8 blend_mode = GetBlendMode();
	written_bytes += file.Write(&blend_mode, sizeof(uint8));

	if (compression_method == ZLIB_COMPRESSION && z_result == Z_OK)
		written_bytes += file.Write(&data_length, sizeof(int64));

	marker = PROJECT_FILE_LAYER_EXTRA_DATA_END_MARKER;
	written_bytes += file.Write(&marker, sizeof(int32));

	return written_bytes;
}


void
Layer::SetName(const char* name)
{
	BString new_name(name);
	fLayerName = new_name.String();
}


Layer*
Layer::ReturnUpperLayer()
{
	if (fImage)
		return fImage->ReturnUpperLayer(this);
	return NULL;
}


Layer*
Layer::ReturnLowerLayer()
{
	if (fImage)
		return fImage->ReturnLowerLayer(this);
	return NULL;
}


const char*
Layer::ReturnProjectName() const
{
	return fImageView->ReturnProjectName();
}
