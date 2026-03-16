#include "tuner_view.h"
#include "vstgui/lib/cstring.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/uidescription/uiattributes.h"

#include <cstdio>
#include <cstring>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace GuitarTuner {

// ============================================================================
// TunerView implementation
// ============================================================================

TunerView::TunerView (const VSTGUI::CRect& size)
    : CView (size)
{
}

TunerView::~TunerView ()
{
    if (timer_)
        timer_->stop ();
}

void TunerView::setNoteName (const char* name)
{
    if (name)
        std::strncpy (noteName_, name, sizeof (noteName_) - 1);
    else
        std::strcpy (noteName_, "-");
    noteName_[sizeof (noteName_) - 1] = '\0';
}

bool TunerView::attached (VSTGUI::CView* parent)
{
    if (!CView::attached (parent))
        return false;

    timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer> (
        [this] (VSTGUI::CVSTGUITimer*) {
            updateSmoothing ();
            invalid ();
        },
        33); // ~30fps

    return true;
}

bool TunerView::removed (VSTGUI::CView* parent)
{
    if (timer_)
    {
        timer_->stop ();
        timer_ = nullptr;
    }
    return CView::removed (parent);
}

void TunerView::updateSmoothing ()
{
    smoothedCents_ += kSmoothingAlpha * (targetCents_ - smoothedCents_);

    // Glow intensity: target 1.0 when in tune (|cents| < 5), else 0.0
    float glowTarget = (confidence_ > 0.1f && targetFrequency_ > 0.0f && std::abs (smoothedCents_) < 5.0f) ? 1.0f : 0.0f;
    glowIntensity_ += kSmoothingAlpha * (glowTarget - glowIntensity_);
}

void TunerView::draw (VSTGUI::CDrawContext* ctx)
{
    VSTGUI::CRect r = getViewSize ();
    drawBackground (ctx, r);
    drawInTuneGlow (ctx, r);
    drawCentMarkers (ctx, r);
    drawTuningArc (ctx, r);
    drawNeedle (ctx, r);
    drawNoteDisplay (ctx, r);
    drawInfoText (ctx, r);
    setDirty (false);
}

void TunerView::drawBackground (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    ctx->setFillColor (VSTGUI::CColor (0x1a, 0x1a, 0x2e, 0xff));
    ctx->drawRect (r, VSTGUI::kDrawFilled);
}

void TunerView::drawInTuneGlow (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    if (glowIntensity_ < 0.01f)
        return;

    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float cy = static_cast<float> (r.top + r.getHeight () * kPivotY);

    // Draw 4 concentric semi-transparent green ellipses
    for (int i = 3; i >= 0; --i)
    {
        float spread = 20.0f + static_cast<float> (i) * 18.0f;
        auto alpha = static_cast<uint8_t> (glowIntensity_ * (20.0f - static_cast<float> (i) * 4.0f));
        VSTGUI::CColor glowColor (0x00, 0xff, 0x88, alpha);
        ctx->setFillColor (glowColor);
        VSTGUI::CRect ellipse (cx - spread, cy - spread * 0.6, cx + spread, cy + spread * 0.6);
        ctx->drawEllipse (ellipse, VSTGUI::kDrawFilled);
    }
}

void TunerView::drawCentMarkers (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float cy = static_cast<float> (r.top + r.getHeight () * kPivotY);
    float radius = static_cast<float> (std::min (r.getWidth (), r.getHeight ()) * kGaugeRadius);

    for (int cents = -50; cents <= 50; cents += 5)
    {
        float angle = static_cast<float> (-M_PI / 2.0) + (static_cast<float> (cents) / 50.0f) * kSweepHalfAngle;

        float innerR = radius * 0.85f;
        float outerR = radius * 0.95f;
        float lineWidth = 1.5f;

        if (cents % 25 == 0)
        {
            innerR = radius * 0.78f;
            outerR = radius * 0.98f;
            lineWidth = 2.5f;
        }
        if (cents == 0)
        {
            innerR = radius * 0.75f;
            outerR = radius;
            lineWidth = 3.0f;
        }

        float x1 = cx + innerR * std::cos (angle);
        float y1 = cy + innerR * std::sin (angle);
        float x2 = cx + outerR * std::cos (angle);
        float y2 = cy + outerR * std::sin (angle);

        if (cents == 0)
        {
            ctx->setFrameColor (VSTGUI::CColor (0x00, 0xff, 0x88, 0xff));
        }
        else
        {
            ctx->setFrameColor (VSTGUI::CColor (0x88, 0x88, 0xaa, 0xff));
        }
        ctx->setLineWidth (lineWidth);

        ctx->drawLine (VSTGUI::CPoint (x1, y1), VSTGUI::CPoint (x2, y2));
    }

    // Draw cent labels at -50, -25, 0, +25, +50
    auto labelFont = VSTGUI::makeOwned<VSTGUI::CFontDesc> ("Arial", 10);
    ctx->setFont (labelFont);
    ctx->setFontColor (VSTGUI::CColor (0x88, 0x88, 0xaa, 0xff));

    const int labelValues[] = { -50, -25, 0, 25, 50 };
    for (int cents : labelValues)
    {
        float angle = static_cast<float> (-M_PI / 2.0) + (static_cast<float> (cents) / 50.0f) * kSweepHalfAngle;
        float labelR = radius * 1.08f;
        float lx = cx + labelR * std::cos (angle);
        float ly = cy + labelR * std::sin (angle);

        char label[8];
        if (cents > 0)
            std::snprintf (label, sizeof (label), "+%d", cents);
        else
            std::snprintf (label, sizeof (label), "%d", cents);

        VSTGUI::CRect labelRect (lx - 20, ly - 7, lx + 20, ly + 7);
        ctx->drawString (VSTGUI::UTF8String (label), labelRect, VSTGUI::kCenterText);
    }
}

void TunerView::drawTuningArc (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float cy = static_cast<float> (r.top + r.getHeight () * kPivotY);
    float radius = static_cast<float> (std::min (r.getWidth (), r.getHeight ()) * kGaugeRadius);

    if (confidence_ < 0.1f || targetFrequency_ <= 0.0f)
        return;

    float arcR = radius * 0.72f;
    float bandWidth = radius * 0.06f;
    int numSegments = 61;

    auto* path = ctx->createGraphicsPath ();
    if (path)
    {
        for (int i = 0; i < numSegments; ++i)
        {
            float segCents = -50.0f + (100.0f * static_cast<float> (i) / static_cast<float> (numSegments - 1));
            float angle = static_cast<float> (-M_PI / 2.0) + (segCents / 50.0f) * kSweepHalfAngle;

            float nextCents = -50.0f + (100.0f * static_cast<float> (i + 1) / static_cast<float> (numSegments - 1));
            if (i == numSegments - 1)
                nextCents = segCents + (100.0f / static_cast<float> (numSegments - 1));
            float nextAngle = static_cast<float> (-M_PI / 2.0) + (nextCents / 50.0f) * kSweepHalfAngle;

            float innerR = arcR - bandWidth * 0.5f;
            float outerR = arcR + bandWidth * 0.5f;

            VSTGUI::CColor color;
            float absCents = std::abs (segCents);
            if (absCents < 5.0f)
                color = VSTGUI::CColor (0x00, 0xcc, 0x66, 0xb0);
            else if (absCents < 15.0f)
                color = VSTGUI::CColor (0xff, 0xcc, 0x00, 0x80);
            else
                color = VSTGUI::CColor (0xff, 0x44, 0x44, 0x60);

            // Draw each segment as a small filled trapezoid
            auto* seg = ctx->createGraphicsPath ();
            if (seg)
            {
                float x0 = cx + innerR * std::cos (angle);
                float y0 = cy + innerR * std::sin (angle);
                float x1 = cx + outerR * std::cos (angle);
                float y1 = cy + outerR * std::sin (angle);
                float x2 = cx + outerR * std::cos (nextAngle);
                float y2 = cy + outerR * std::sin (nextAngle);
                float x3 = cx + innerR * std::cos (nextAngle);
                float y3 = cy + innerR * std::sin (nextAngle);

                seg->beginSubpath (VSTGUI::CPoint (x0, y0));
                seg->addLine (VSTGUI::CPoint (x1, y1));
                seg->addLine (VSTGUI::CPoint (x2, y2));
                seg->addLine (VSTGUI::CPoint (x3, y3));
                seg->closeSubpath ();

                ctx->setFillColor (color);
                ctx->drawGraphicsPath (seg, VSTGUI::CDrawContext::kPathFilled);
                seg->forget ();
            }
        }
        path->forget ();
    }
    else
    {
        // Fallback: dot approach
        int numDots = 21;
        for (int i = 0; i < numDots; i++)
        {
            float dotCents = -50.0f + (100.0f * static_cast<float> (i) / static_cast<float> (numDots - 1));
            float angle = static_cast<float> (-M_PI / 2.0) + (dotCents / 50.0f) * kSweepHalfAngle;

            float x = cx + arcR * std::cos (angle);
            float y = cy + arcR * std::sin (angle);

            VSTGUI::CColor color;
            float absCents = std::abs (dotCents);
            if (absCents < 5.0f)
                color = VSTGUI::CColor (0x00, 0xcc, 0x66, 0x80);
            else if (absCents < 15.0f)
                color = VSTGUI::CColor (0xff, 0xcc, 0x00, 0x60);
            else
                color = VSTGUI::CColor (0xff, 0x44, 0x44, 0x40);

            ctx->setFillColor (color);
            VSTGUI::CRect dot (x - 3, y - 3, x + 3, y + 3);
            ctx->drawEllipse (dot, VSTGUI::kDrawFilled);
        }
    }
}

void TunerView::drawNeedle (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    if (confidence_ < 0.05f || targetFrequency_ <= 0.0f)
        return;

    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float cy = static_cast<float> (r.top + r.getHeight () * kPivotY);
    float needleLen = static_cast<float> (std::min (r.getWidth (), r.getHeight ()) * kNeedleLength);

    float clampedCents = smoothedCents_;
    if (clampedCents < -50.0f) clampedCents = -50.0f;
    if (clampedCents > 50.0f) clampedCents = 50.0f;

    float angle = static_cast<float> (-M_PI / 2.0) + (clampedCents / 50.0f) * kSweepHalfAngle;

    // Needle tip
    float nx = cx + needleLen * std::cos (angle);
    float ny = cy + needleLen * std::sin (angle);

    // Color based on tuning accuracy
    VSTGUI::CColor needleColor;
    float absCents = std::abs (smoothedCents_);
    if (absCents < 5.0f)
        needleColor = VSTGUI::CColor (0x00, 0xff, 0x88, 0xff);
    else if (absCents < 15.0f)
        needleColor = VSTGUI::CColor (0xff, 0xdd, 0x00, 0xff);
    else
        needleColor = VSTGUI::CColor (0xff, 0x44, 0x44, 0xff);

    // Tapered triangle needle via CGraphicsPath
    float perpAngle = angle + static_cast<float> (M_PI / 2.0);
    float baseHalf = 4.0f;
    float tipHalf = 1.0f;

    auto* path = ctx->createGraphicsPath ();
    if (path)
    {
        float bx1 = cx + baseHalf * std::cos (perpAngle);
        float by1 = cy + baseHalf * std::sin (perpAngle);
        float bx2 = cx - baseHalf * std::cos (perpAngle);
        float by2 = cy - baseHalf * std::sin (perpAngle);
        float tx1 = nx + tipHalf * std::cos (perpAngle);
        float ty1 = ny + tipHalf * std::sin (perpAngle);
        float tx2 = nx - tipHalf * std::cos (perpAngle);
        float ty2 = ny - tipHalf * std::sin (perpAngle);

        path->beginSubpath (VSTGUI::CPoint (bx1, by1));
        path->addLine (VSTGUI::CPoint (tx1, ty1));
        path->addLine (VSTGUI::CPoint (tx2, ty2));
        path->addLine (VSTGUI::CPoint (bx2, by2));
        path->closeSubpath ();

        ctx->setFillColor (needleColor);
        ctx->drawGraphicsPath (path, VSTGUI::CDrawContext::kPathFilled);
        path->forget ();
    }
    else
    {
        // Fallback: thick line
        ctx->setFrameColor (needleColor);
        ctx->setLineWidth (3.0);
        ctx->drawLine (VSTGUI::CPoint (cx, cy), VSTGUI::CPoint (nx, ny));
    }

    // Pivot dot with border ring
    ctx->setFillColor (VSTGUI::CColor (0xcc, 0xcc, 0xcc, 0xff));
    VSTGUI::CRect pivot (cx - 8, cy - 8, cx + 8, cy + 8);
    ctx->drawEllipse (pivot, VSTGUI::kDrawFilled);
    ctx->setFrameColor (VSTGUI::CColor (0x88, 0x88, 0xaa, 0xff));
    ctx->setLineWidth (1.5);
    ctx->drawEllipse (pivot, VSTGUI::kDrawStroked);
}

void TunerView::drawNoteDisplay (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float noteY = static_cast<float> (r.top + r.getHeight () * kPivotY + 16);

    VSTGUI::CColor textColor (0xff, 0xff, 0xff, 0xff);
    if (confidence_ < 0.1f || targetFrequency_ <= 0.0f)
        textColor = VSTGUI::CColor (0x66, 0x66, 0x88, 0xff);

    auto noteFont = VSTGUI::makeOwned<VSTGUI::CFontDesc> ("Arial", 42, VSTGUI::kBoldFace);
    ctx->setFont (noteFont);
    ctx->setFontColor (textColor);

    VSTGUI::CRect noteRect (cx - 80, noteY, cx + 80, noteY + 48);
    VSTGUI::UTF8String noteStr (noteName_);
    ctx->drawString (noteStr, noteRect, VSTGUI::kCenterText);
}

void TunerView::drawInfoText (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    auto smallFont = VSTGUI::makeOwned<VSTGUI::CFontDesc> ("Arial", 12);
    ctx->setFont (smallFont);

    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float infoY = static_cast<float> (r.top + r.getHeight () * kPivotY + 68);

    // Frequency display
    char freqStr[32];
    if (targetFrequency_ > 0.0f)
        std::snprintf (freqStr, sizeof (freqStr), "%.1f Hz", targetFrequency_);
    else
        std::snprintf (freqStr, sizeof (freqStr), "--- Hz");

    ctx->setFontColor (VSTGUI::CColor (0xaa, 0xaa, 0xcc, 0xff));
    VSTGUI::CRect freqRect (cx - 80, infoY, cx + 80, infoY + 16);
    ctx->drawString (VSTGUI::UTF8String (freqStr), freqRect, VSTGUI::kCenterText);

    // Cents display
    char centsStr[32];
    if (targetFrequency_ > 0.0f)
    {
        if (targetCents_ >= 0.0f)
            std::snprintf (centsStr, sizeof (centsStr), "+%.1f cents", targetCents_);
        else
            std::snprintf (centsStr, sizeof (centsStr), "%.1f cents", targetCents_);
    }
    else
    {
        std::snprintf (centsStr, sizeof (centsStr), "--- cents");
    }

    VSTGUI::CRect centsRect (cx - 80, infoY + 16, cx + 80, infoY + 32);
    ctx->drawString (VSTGUI::UTF8String (centsStr), centsRect, VSTGUI::kCenterText);

    // Reference display
    char refStr[32];
    std::snprintf (refStr, sizeof (refStr), "A4=%.0f Hz", reference_);

    ctx->setFontColor (VSTGUI::CColor (0x88, 0x88, 0xaa, 0xff));
    VSTGUI::CRect refRect (cx - 80, infoY + 34, cx + 80, infoY + 48);
    ctx->drawString (VSTGUI::UTF8String (refStr), refRect, VSTGUI::kCenterText);

    // Title
    auto titleFont = VSTGUI::makeOwned<VSTGUI::CFontDesc> ("Arial", 14, VSTGUI::kBoldFace);
    ctx->setFont (titleFont);
    ctx->setFontColor (VSTGUI::CColor (0x88, 0x99, 0xcc, 0xff));
    VSTGUI::CRect titleRect (r.left + 10, r.top + 8, r.right - 10, r.top + 26);
    ctx->drawString (VSTGUI::UTF8String ("Guitar Tuner"), titleRect, VSTGUI::kCenterText);
}

// ============================================================================
// TunerViewFactory implementation
// ============================================================================

TunerViewFactory::TunerViewFactory ()
{
    VSTGUI::UIViewFactory::registerViewCreator (*this);
}

VSTGUI::CView* TunerViewFactory::create (const VSTGUI::UIAttributes& attributes,
                                           const VSTGUI::IUIDescription* description) const
{
    VSTGUI::CRect size (0, 0, 400, 370);
    const std::string* sizeStr = attributes.getAttributeValue ("size");
    if (sizeStr)
    {
        VSTGUI::CPoint p;
        if (VSTGUI::UIAttributes::stringToPoint (*sizeStr, p))
        {
            size.setWidth (p.x);
            size.setHeight (p.y);
        }
    }
    return new TunerView (size);
}

// Static factory instance to auto-register
static TunerViewFactory gTunerViewFactory;

} // namespace GuitarTuner
