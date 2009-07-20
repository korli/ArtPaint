/*
 * Copyright 2003, Heikki Suhonen
 * Copyright 2009, Karsten Heimrich
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		Heikki Suhonen <heikki.suhonen@gmail.com>
 *		Karsten Heimrich <host.haiku@gmx.de>
 *
 */

#include "DrawingTool.h"

#include "Cursors.h"
#include "StringServer.h"


#include <CheckBox.h>
#include <ClassInfo.h>
#include <File.h>
#include <GroupLayout.h>
#include <Handler.h>
#include <StringView.h>


#include <string.h>


DrawingTool::DrawingTool(const BString& name, int32 type)
	: fIcon(NULL)
	, fName(name)
	, fType(type)
	, fLastUpdatedRect(BRect())
{
	// In derived classes set whatever options tool happens to use.
	options = 0;
	number_of_options = 0;
}


DrawingTool::~DrawingTool()
{
}


ToolScript*
DrawingTool::UseTool(ImageView*, uint32, BPoint, BPoint)
{
	// This function will do the drawing in the derived classes. ImageView must
	// provide necessary data with a function that can be called from here.
	return NULL;
}


int32
DrawingTool::UseToolWithScript(ToolScript*, BBitmap*)
{
	return B_OK;
}


BView*
DrawingTool::makeConfigView()
{
	BView* configView = new DrawingToolConfigView(this);
	configView->SetLayout(new BGroupLayout(B_VERTICAL));
	configView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	BStringView* stringView =
		new BStringView("", StringServer::ReturnString(NO_OPTIONS_STRING));
	configView->AddChild(stringView);
	stringView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	return configView;
}


void
DrawingTool::SetOption(int32 option, int32 value, BHandler *source)
{
	// If option is valid for this tool, set it.
	// If handler is NULL, the boolean options should use value as the new value.
	// Otherwise they should use value that can be gotten from the source.
	;
	BControl *control;
	if (option & options) {
		switch (option) {
			case SIZE_OPTION:
				settings.size = value;
				break;
			case PRESSURE_OPTION:
				settings.pressure = value;
				break;

			case MODE_OPTION: {
				settings.mode = value;
				if (BCheckBox* booleanBox = dynamic_cast<BCheckBox*>(source))
					settings.mode = booleanBox->Value();
			} break;

			case SHAPE_OPTION:
				settings.shape = value;
				break;
			case GRADIENT_ENABLED_OPTION:
				control = cast_as(source,BControl);
				if (control != NULL) {
					settings.gradient_enabled = control->Value();
				}
				else {
					settings.gradient_enabled = value;
				}
				break;
			case GRADIENT_COLOR_OPTION:
				settings.gradient_color = value;
				break;
			case PREVIEW_ENABLED_OPTION:
				control = cast_as(source,BControl);
				if (control != NULL) {
					settings.preview_enabled = control->Value();
				}
				else {
					settings.preview_enabled = value;
				}
				break;
			case FILL_ENABLED_OPTION:
				control = cast_as(source,BControl);
				if (control != NULL) {
					settings.fill_enabled = control->Value();
				}
				else {
					settings.fill_enabled = value;
				}
				break;
			case ROTATION_ENABLED_OPTION:
				control = cast_as(source,BControl);
				if (control != NULL) {
					settings.rotation_enabled = control->Value();
				}
				else {
					settings.rotation_enabled = value;
				}
				break;
			case ANTI_ALIASING_LEVEL_OPTION:
				control = cast_as(source,BControl);
				if (control != NULL) {
					settings.anti_aliasing_level = control->Value();
				}
				else {
					settings.anti_aliasing_level = value;
				}
				break;
			case CONTINUITY_OPTION:
				control = cast_as(source,BControl);
				if (control != NULL) {
					settings.continuity = control->Value();
				}
				else {
					settings.continuity = value;
				}
				break;
			case TOLERANCE_OPTION:
				settings.tolerance = value;
				break;
			case TRANSPARENCY_OPTION:
				settings.transparency = value;
				break;
			default:
				break;
		}
	}
}


int32
DrawingTool::GetCurrentValue(int32 option)
{
	if (option & options) {
		switch (option) {
			case SIZE_OPTION:
				return settings.size;
			case PRESSURE_OPTION:
				return settings.pressure;
			case MODE_OPTION:
				return settings.mode;
			case SHAPE_OPTION:
				return settings.shape;
			case GRADIENT_ENABLED_OPTION:
				return settings.gradient_enabled;
			case GRADIENT_COLOR_OPTION:
				return settings.gradient_color;
			case PREVIEW_ENABLED_OPTION:
				return settings.preview_enabled;
			case FILL_ENABLED_OPTION:
				return settings.fill_enabled;
			case ROTATION_ENABLED_OPTION:
				return settings.rotation_enabled;
			case ANTI_ALIASING_LEVEL_OPTION:
				return settings.anti_aliasing_level;
			case CONTINUITY_OPTION:
				return settings.continuity;
			case TOLERANCE_OPTION:
				return settings.tolerance;
			case TRANSPARENCY_OPTION:
				return settings.transparency;
			default:
				return 0;
		}
	}
	return 0;
}


status_t
DrawingTool::readSettings(BFile &file, bool isLittleEndian)
{
	int32 length;
	if (file.Read(&length,sizeof(int32)) != sizeof(int32))
		return B_ERROR;

	if (isLittleEndian)
		length = B_LENDIAN_TO_HOST_INT32(length);
	else
		length = B_BENDIAN_TO_HOST_INT32(length);

	int32 version;
	if (file.Read(&version,sizeof(int32)) != sizeof(int32))
		return B_ERROR;

	if (isLittleEndian)
		version = B_LENDIAN_TO_HOST_INT32(version);
	else
		version = B_BENDIAN_TO_HOST_INT32(version);

	if (version != TOOL_SETTINGS_STRUCT_VERSION) {
		file.Seek(length - sizeof(int32), SEEK_CUR);
		return B_ERROR;
	} else {
		// This should also be converted to right endianness
		int32 settingsSize = sizeof(struct tool_settings);
		if (file.Read(&settings, settingsSize) != settingsSize)
			return B_ERROR;
	}

	return B_OK;
}


status_t
DrawingTool::writeSettings(BFile &file)
{
	if (file.Write(&fType,sizeof(int32)) != sizeof(int32))
		return B_ERROR;

	int32 settingsSize = sizeof(struct tool_settings) + sizeof(int32);
	if (file.Write(&settingsSize,sizeof(int32)) != sizeof(int32))
		return B_ERROR;

	int32 settingsVersion = TOOL_SETTINGS_STRUCT_VERSION;
	if (file.Write(&settingsVersion,sizeof(int32)) != sizeof(int32))
		return B_ERROR;

	settingsSize = sizeof(struct tool_settings);
	if (file.Write(&settings, settingsSize) != settingsSize)
		return B_ERROR;

	return B_OK;
}


BRect
DrawingTool::LastUpdatedRect() const
{
	return fLastUpdatedRect;
}


void
DrawingTool::SetLastUpdatedRect(const BRect& rect)
{
	fLastUpdatedRect = rect;
}


const void*
DrawingTool::ToolCursor() const
{
	return HS_CROSS_CURSOR;
}


const char*
DrawingTool::HelpString(bool isInUse) const
{
	return StringServer::ReturnString(isInUse ? USING_THE_TOOL_STRING
		: USE_THE_TOOL_STRING);
}


// #pragma mark -- DrawingToolConfigView


DrawingToolConfigView::DrawingToolConfigView(DrawingTool* newTool)
	: BView("drawing tool config view", 0, NULL)
	, tool(newTool)
{
}


DrawingToolConfigView::~DrawingToolConfigView()
{
}


void
DrawingToolConfigView::AttachedToWindow()
{
	if (Parent())
		SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


void
DrawingToolConfigView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case OPTION_CHANGED: {
			// This comes from one of the controls in this window, it tells us
			// that the value for tool control(s) have changed, it contains
			// int32 "option" and int32 "value" data members
			BHandler* handler;
			message->FindPointer("source", (void**)&handler);
			if (tool) {
				tool->SetOption(message->FindInt32("option"),
					message->FindInt32("value"), handler);
			}
		}	break;

		default: {
			BView::MessageReceived(message);
		}	break;
	}
}
