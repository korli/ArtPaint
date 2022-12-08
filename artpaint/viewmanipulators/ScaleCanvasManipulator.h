/*
 * Copyright 2003, Heikki Suhonen
 * Copyright 2009, Karsten Heimrich
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		Heikki Suhonen <heikki.suhonen@gmail.com>
 * 		Karsten Heimrich <host.haiku@gmx.de>
 *
 */
#ifndef SCALE_CANVAS_MANIPULATOR_H
#define SCALE_CANVAS_MANIPULATOR_H

#include "ManipulatorSettings.h"
#include "Selection.h"
#include "WindowGUIManipulator.h"


#include <Messenger.h>


#define	WIDTH_CHANGED		'Wich'
#define	HEIGHT_CHANGED		'Hech'

#define MULTIPLY_WIDTH		'mlWi'
#define MULTIPLY_HEIGHT		'mlHe'

#define RESTORE_WIDTH		'Rswd'
#define RESTORE_HEIGHT		'Rshg'

#define	PROPORTION_CHANGED	'Prpc'

class BButton;
class ScaleCanvasManipulatorView;


namespace ArtPaint {
	namespace Interface {
		class NumberControl;
	}
}
using ArtPaint::Interface::NumberControl;


class ScaleCanvasManipulatorSettings : public ManipulatorSettings {
public:
	ScaleCanvasManipulatorSettings()
		: ManipulatorSettings() {
		width_coefficient = 1.0;
		height_coefficient = 1.0;
	}

	ScaleCanvasManipulatorSettings(ScaleCanvasManipulatorSettings *s)
		: ManipulatorSettings() {
		width_coefficient = s->width_coefficient;
		height_coefficient = s->height_coefficient;
	}


	float	height_coefficient;
	float	width_coefficient;
};



class ScaleCanvasManipulator : public WindowGUIManipulator {
	BBitmap*	ManipulateBitmap(BBitmap* b, BStatusBar* stb)
	{ return WindowGUIManipulator::ManipulateBitmap(b, stb); }

	BBitmap*	preview_bitmap;
	BBitmap*	copy_of_the_preview_bitmap;

	ScaleCanvasManipulatorView		*configuration_view;
	ScaleCanvasManipulatorSettings	*settings;

	float		original_width;
	float		original_height;

	Selection*	selection;
	
public:
	ScaleCanvasManipulator(BBitmap*);
	~ScaleCanvasManipulator();

	BBitmap*	ManipulateBitmap(ManipulatorSettings*, BBitmap *original,
					BStatusBar*);
	int32		PreviewBitmap(bool, BRegion* =NULL);

	void		MouseDown(BPoint, uint32, BView*, bool);
	void		SetValues(float, float);

	BView*		MakeConfigurationView(const BMessenger& target);
	void		Reset();
	void		SetPreviewBitmap(BBitmap*);

	const	char*	ReturnHelpString();
	const	char*	ReturnName();

	ManipulatorSettings*	ReturnSettings();
	void		SetSelection(Selection* new_selection);

};


class ScaleCanvasManipulatorView : public WindowGUIManipulatorView {
public:
								ScaleCanvasManipulatorView(ScaleCanvasManipulator*,
									const BMessenger& target);
	virtual						~ScaleCanvasManipulatorView() {}

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			bool				MaintainProportions() {
									return maintain_proportions;
								}
			void				SetValues(float width, float height);

private:
			void				_SetTarget(BView* view);
			void				_SetValues(float width, float height);
			BButton*			_MakeButton(const char* label,
									uint32 what, float coefficient);

private:
			BMessenger			fTarget;
			float				original_width;
			float				original_height;
			float				current_width;
			float				current_height;
			bool				maintain_proportions;

			ScaleCanvasManipulator*	fManipulator;
			NumberControl*		width_control;
			NumberControl*		height_control;
};

#endif