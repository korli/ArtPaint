/*
 * Copyright 2003, Heikki Suhonen
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		Heikki Suhonen <heikki.suhonen@gmail.com>
 *
 */
#include "ToolSelectionWindow.h"

#include "Controls.h"
#include "DrawingTools.h"
#include "FloaterManager.h"
#include "MatrixView.h"
#include "MessageConstants.h"
#include "MessageFilters.h"
#include "PaintApplication.h"
#include "Settings.h"
#include "StringServer.h"
#include "Tools.h"
#include "ToolButton.h"
#include "ToolManager.h"
#include "ToolSetupWindow.h"
#include "ResourceServer.h"
#include "UtilityClasses.h"


ToolSelectionWindow* ToolSelectionWindow::fSelectionWindow = NULL;


ToolSelectionWindow::ToolSelectionWindow(BRect frame)
	: BWindow(frame, StringServer::ReturnString(TOOLS_STRING),
		B_FLOATING_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, B_NOT_H_RESIZABLE |
		B_NOT_ZOOMABLE | B_WILL_ACCEPT_FIRST_CLICK | B_AVOID_FRONT)
{
	int32 pictureSize = LARGE_TOOL_ICON_SIZE + 4.0;
	global_settings* settings = ((PaintApplication*)be_app)->GlobalSettings();
	settings->tool_select_window_visible = true;

	int32 activeTool = tool_manager->ReturnActiveToolType();
	fMatrixView = new MatrixView(pictureSize, pictureSize, EXTRA_EDGE);

	_AddTool(tool_manager->ReturnTool(FREE_LINE_TOOL), activeTool);
	_AddTool(tool_manager->ReturnTool(STRAIGHT_LINE_TOOL), activeTool);
	_AddTool(tool_manager->ReturnTool(RECTANGLE_TOOL), activeTool);
	_AddTool(tool_manager->ReturnTool(ELLIPSE_TOOL), activeTool);

	// Here we could add a separator to the tool window.

	_AddTool(tool_manager->ReturnTool(BRUSH_TOOL), activeTool);
	_AddTool(tool_manager->ReturnTool(HAIRY_BRUSH_TOOL), activeTool);
	_AddTool(tool_manager->ReturnTool(AIR_BRUSH_TOOL), activeTool);
	_AddTool(tool_manager->ReturnTool(BLUR_TOOL), activeTool);
	_AddTool(tool_manager->ReturnTool(FILL_TOOL), activeTool);
	_AddTool(tool_manager->ReturnTool(TEXT_TOOL), activeTool);
	_AddTool(tool_manager->ReturnTool(TRANSPARENCY_TOOL), activeTool);
	_AddTool(tool_manager->ReturnTool(ERASER_TOOL), activeTool);
	_AddTool(tool_manager->ReturnTool(SELECTOR_TOOL), activeTool);
	_AddTool(tool_manager->ReturnTool(COLOR_SELECTOR_TOOL), activeTool);

	AddChild(fMatrixView);
	fMatrixView->ResizeTo(Bounds().Width(), Bounds().Height());

	window_feel feel = settings->tool_select_window_feel;
	SetFeel(feel);
	if (feel == B_NORMAL_WINDOW_FEEL)
		SetLook(B_TITLED_WINDOW_LOOK);
	else
		SetLook(B_FLOATING_WINDOW_LOOK);

	float minDimension = 2 * EXTRA_EDGE + pictureSize;
	float maxDimension = 1 + EXTRA_EDGE + fMatrixView->CountChildren() *
		(pictureSize + EXTRA_EDGE);
	SetSizeLimits(minDimension, maxDimension, minDimension, maxDimension);

	if (Lock()) {
		AddCommonFilter(new BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE,
			B_MOUSE_DOWN, window_activation_filter));
		AddCommonFilter(new BMessageFilter(B_KEY_DOWN, AppKeyFilterFunction));
		Unlock();
	}

	Show();

	// NOTE: this is broken/ not implemented in Haiku, so the tools window
	//		 will not show up horizontal as it should be, enable if implemented
	//SetWindowAlignment(B_PIXEL_ALIGNMENT, 0, 0, picture_size + EXTRA_EDGE,
	//	EXTRA_EDGE + 1, 0, 0, picture_size + EXTRA_EDGE, EXTRA_EDGE + 1);

	// remove this if SetWindowAlignment is implemented
	ResizeBy(0.0, maxDimension - pictureSize);

	fSelectionWindow = this;
	FloaterManager::AddFloater(this);
}


ToolSelectionWindow::~ToolSelectionWindow()
{
	// Record our frame to the settings.
	global_settings* settings = ((PaintApplication*)be_app)->GlobalSettings();
	settings->tool_select_window_frame = Frame();
	settings->tool_select_window_visible = false;

	fSelectionWindow = NULL;
	FloaterManager::RemoveFloater(this);
}


void
ToolSelectionWindow::FrameResized(float width, float height)
{
	BView* matrix = ChildAt(0);
	if (matrix != NULL) {
		float tmpHeight;
		matrix->GetPreferredSize(&width, &tmpHeight);
		if ((width != Bounds().Width()) || (tmpHeight != Bounds().Height()))
			ResizeTo(width, height);
	}
}


void
ToolSelectionWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case HS_TOOL_CHANGED: {
			int32 tool;
			if (message->FindInt32("tool", &tool) == B_OK) {
				tool_manager->ChangeTool(tool);

				uint32 button;
				if (message->FindUInt32("buttons", &button) == B_OK
					&& (button & B_SECONDARY_MOUSE_BUTTON)) {
					ToolSetupWindow::ShowToolSetupWindow(tool);
				}
			}
		} break;

		default: {
			BWindow::MessageReceived(message);
		}	break;
	}
}


void
ToolSelectionWindow::showWindow()
{
	if (fSelectionWindow) {
		if (fSelectionWindow->Lock()) {
			fSelectionWindow->SetWorkspaces(B_CURRENT_WORKSPACE);

			if (fSelectionWindow->IsHidden())
				fSelectionWindow->Show();

			if (!fSelectionWindow->IsActive())
				fSelectionWindow->Activate(true);

			fSelectionWindow->Unlock();
		}
	} else {
		global_settings* settings = ((PaintApplication*)be_app)->GlobalSettings();
		new ToolSelectionWindow(settings->tool_select_window_frame);
	}

	fSelectionWindow->MoveTo(FitRectToScreen(fSelectionWindow->Frame()).LeftTop());
}


void
ToolSelectionWindow::setFeel(window_feel feel)
{
	((PaintApplication*)be_app)->GlobalSettings()->tool_select_window_feel = feel;

	if (fSelectionWindow) {
		fSelectionWindow->SetFeel(feel);

		window_look look = B_TITLED_WINDOW_LOOK;
		if (feel != B_NORMAL_WINDOW_FEEL)
			look = B_FLOATING_WINDOW_LOOK;
		fSelectionWindow->SetLook(look);
	}
}


void
ToolSelectionWindow::ChangeTool(int32 tool)
{
//	if (fSelectionWindow && fSelectionWindow->Lock()) {
//		ToolButton::ChangeActiveButton(tool);
//		fSelectionWindow->Unlock();
//	}
}


void
ToolSelectionWindow::_AddTool(const DrawingTool* tool, int32 activeTool)
{
	int32 toolType = tool->Type();

	BMessage* message = new BMessage(HS_TOOL_CHANGED);
	message->AddUInt32("buttons", 0);
	message->AddInt32("tool", toolType);

	BBitmap* icon;
	ResourceServer::Instance()->GetBitmap(B_VECTOR_ICON_TYPE, toolType,
		LARGE_TOOL_ICON_SIZE, LARGE_TOOL_ICON_SIZE, &icon);

	ToolButton* button = new ToolButton(tool->Name(), message, icon);
	button->ResizeToPreferred();
	fMatrixView->AddSubView(button);

	if (toolType == activeTool)
		button->SetValue(B_CONTROL_ON);
}
