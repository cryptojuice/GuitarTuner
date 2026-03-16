#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/clinestyle.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"

#include <atomic>
#include <cmath>

namespace GuitarTuner {

class TunerView : public VSTGUI::CView
{
public:
    TunerView (const VSTGUI::CRect& size);
    ~TunerView () override;

    void draw (VSTGUI::CDrawContext* context) override;

    // Called by controller to update display values
    void setFrequency (float freq)    { targetFrequency_ = freq; }
    void setCents (float cents)       { targetCents_ = cents; }
    void setNoteName (const char* name);
    void setConfidence (float conf)   { confidence_ = conf; }
    void setReference (float ref)     { reference_ = ref; }

    // Attach/detach from frame for timer
    bool attached (VSTGUI::CView* parent) override;
    bool removed (VSTGUI::CView* parent) override;

    CLASS_METHODS (TunerView, VSTGUI::CView)

private:
    void drawBackgroundGradient (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r);
    void drawArcTrack (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r);
    void drawArcHighlight (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r);
    void drawNeedle (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r);
    void drawNeedleTrail (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r);
    void drawNeedleGlow (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r);
    void drawNoteDisplay (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r);
    void drawInfoText (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r);
    void drawCentMarkers (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r);
    void drawNoteBloom (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r);

    void updateSmoothing ();

    // Layout constants
    static constexpr float kPivotY = 0.58f;
    static constexpr float kGaugeRadius = 0.38f;
    static constexpr float kNeedleLength = 0.35f;
    static constexpr float kSweepHalfAngle = static_cast<float> (3.14159265358979323846 / 3.0);

    // Smoothing constants
    static constexpr float kSmoothingAlphaFast = 0.25f;
    static constexpr float kSmoothingAlphaSlow = 0.08f;

    // Trail & bloom constants
    static constexpr int kTrailLength = 8;
    static constexpr float kInTuneThreshold = 3.0f;
    static constexpr float kArcHighlightHalfWidth = 8.0f;

    // Display state
    float targetFrequency_ = 0.0f;
    float targetCents_ = 0.0f;
    float smoothedCents_ = 0.0f;
    float confidence_ = 0.0f;
    float bloomIntensity_ = 0.0f;
    float reference_ = 440.0f;
    char noteName_[8] = {'-', '\0'};

    // Trail ring buffer
    float trailCents_[kTrailLength] = {};
    int trailIndex_ = 0;
    int trailCount_ = 0;

    // Timer for refresh
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

// Factory for creating TunerView from uidesc
class TunerViewFactory : public VSTGUI::ViewCreatorAdapter
{
public:
    TunerViewFactory ();

    VSTGUI::IdStringPtr getViewName () const override { return "TunerView"; }
    VSTGUI::IdStringPtr getBaseViewName () const override { return "CView"; }
    VSTGUI::CView* create (const VSTGUI::UIAttributes& attributes,
                            const VSTGUI::IUIDescription* description) const override;
};

} // namespace GuitarTuner
