#include "tuner_view.h"
#include "vstgui/lib/cstring.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/uidescription/uiattributes.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

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
    // Adaptive smoothing: fast for large jumps, slow for fine-tuning
    float delta = targetCents_ - smoothedCents_;
    float alpha = (std::abs (delta) > 15.0f) ? kSmoothingAlphaFast : kSmoothingAlphaSlow;
    smoothedCents_ += alpha * delta;

    // Record into trail ring buffer
    trailCents_[trailIndex_] = smoothedCents_;
    trailIndex_ = (trailIndex_ + 1) % kTrailLength;
    if (trailCount_ < kTrailLength)
        trailCount_++;

    // Bloom intensity: target 1.0 when in tune (|cents| < kInTuneThreshold), else 0.0
    float bloomTarget = (confidence_ > 0.1f && targetFrequency_ > 0.0f && std::abs (smoothedCents_) < kInTuneThreshold) ? 1.0f : 0.0f;
    bloomIntensity_ += 0.2f * (bloomTarget - bloomIntensity_);
}

void TunerView::draw (VSTGUI::CDrawContext* ctx)
{
    VSTGUI::CRect r = getViewSize ();
    drawBackgroundGradient (ctx, r);
    drawArcTrack (ctx, r);
    drawArcHighlight (ctx, r);
    drawCentMarkers (ctx, r);
    drawNeedleTrail (ctx, r);
    drawNeedleGlow (ctx, r);
    drawNeedle (ctx, r);
    drawNoteBloom (ctx, r);
    drawNoteDisplay (ctx, r);
    drawInfoText (ctx, r);
    setDirty (false);
}

void TunerView::drawBackgroundGradient (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    // Solid deep navy fill
    ctx->setFillColor (VSTGUI::CColor (0x0f, 0x0f, 0x1a, 0xff));
    ctx->drawRect (r, VSTGUI::kDrawFilled);

    // Radial gradient spotlight centered at pivot
    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float cy = static_cast<float> (r.top + r.getHeight () * kPivotY);
    float spotRadius = static_cast<float> (std::min (r.getWidth (), r.getHeight ()) * 0.5);

    auto* path = ctx->createGraphicsPath ();
    if (path)
    {
        path->addRect (VSTGUI::CRect (r));
        auto gradient = VSTGUI::owned (VSTGUI::CGradient::create (0.0, 1.0,
            VSTGUI::CColor (0x1a, 0x1a, 0x30, 0xff),
            VSTGUI::CColor (0x0f, 0x0f, 0x1a, 0xff)));
        ctx->fillRadialGradient (path, *gradient,
            VSTGUI::CPoint (cx, cy), spotRadius);
        path->forget ();
    }
}

void TunerView::drawArcTrack (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    if (confidence_ < 0.1f || targetFrequency_ <= 0.0f)
        return;

    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float cy = static_cast<float> (r.top + r.getHeight () * kPivotY);
    float radius = static_cast<float> (std::min (r.getWidth (), r.getHeight ()) * kGaugeRadius);

    float arcR = radius * 0.72f;
    float bandWidth = radius * 0.06f;
    int numSegments = 31;

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

        // Dim uniform color for guide rail
        VSTGUI::CColor color (0x2a, 0x2a, 0x3e, 0x40);

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
}

void TunerView::drawArcHighlight (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    if (confidence_ < 0.1f || targetFrequency_ <= 0.0f)
        return;

    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float cy = static_cast<float> (r.top + r.getHeight () * kPivotY);
    float radius = static_cast<float> (std::min (r.getWidth (), r.getHeight ()) * kGaugeRadius);

    float arcR = radius * 0.72f;
    float bandWidth = radius * 0.06f;
    int numSegments = 31;

    // Determine accent color based on tuning accuracy
    float absCents = std::abs (smoothedCents_);
    VSTGUI::CColor accentColor;
    if (absCents < 5.0f)
        accentColor = VSTGUI::CColor (0x00, 0xe8, 0x7b, 0xff);
    else if (absCents < 15.0f)
        accentColor = VSTGUI::CColor (0xff, 0xb8, 0x30, 0xff);
    else
        accentColor = VSTGUI::CColor (0xff, 0x44, 0x66, 0xff);

    for (int i = 0; i < numSegments; ++i)
    {
        float segCents = -50.0f + (100.0f * static_cast<float> (i) / static_cast<float> (numSegments - 1));

        // Only draw segments within highlight range of needle
        float dist = std::abs (segCents - smoothedCents_);
        if (dist > kArcHighlightHalfWidth)
            continue;

        float alphaFactor = (1.0f - dist / kArcHighlightHalfWidth) * 0.9f;
        auto alpha = static_cast<uint8_t> (alphaFactor * 255.0f);

        float angle = static_cast<float> (-M_PI / 2.0) + (segCents / 50.0f) * kSweepHalfAngle;
        float nextCents = -50.0f + (100.0f * static_cast<float> (i + 1) / static_cast<float> (numSegments - 1));
        if (i == numSegments - 1)
            nextCents = segCents + (100.0f / static_cast<float> (numSegments - 1));
        float nextAngle = static_cast<float> (-M_PI / 2.0) + (nextCents / 50.0f) * kSweepHalfAngle;

        float innerR = arcR - bandWidth * 0.5f;
        float outerR = arcR + bandWidth * 0.5f;

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

            VSTGUI::CColor segColor (accentColor.red, accentColor.green, accentColor.blue, alpha);
            ctx->setFillColor (segColor);
            ctx->drawGraphicsPath (seg, VSTGUI::CDrawContext::kPathFilled);
            seg->forget ();
        }
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
            // Center tick modulates brightness with bloom
            auto green = static_cast<uint8_t> (0x88 + bloomIntensity_ * (0xff - 0x88));
            ctx->setFrameColor (VSTGUI::CColor (0x00, green, 0x88, 0xff));
        }
        else if (cents % 25 == 0)
        {
            // Major ticks
            ctx->setFrameColor (VSTGUI::CColor (0x88, 0x88, 0xaa, 0xff));
        }
        else
        {
            // Minor ticks dimmed
            ctx->setFrameColor (VSTGUI::CColor (0x55, 0x55, 0x70, 0xff));
        }
        ctx->setLineWidth (lineWidth);
        ctx->drawLine (VSTGUI::CPoint (x1, y1), VSTGUI::CPoint (x2, y2));
    }

    // Draw cent labels at -50, -25, 0, +25, +50
    auto labelFont = VSTGUI::makeOwned<VSTGUI::CFontDesc> ("Segoe UI", 10);
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

void TunerView::drawNeedleTrail (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    if (confidence_ < 0.05f || targetFrequency_ <= 0.0f || trailCount_ < 2)
        return;

    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float cy = static_cast<float> (r.top + r.getHeight () * kPivotY);
    float needleLen = static_cast<float> (std::min (r.getWidth (), r.getHeight ()) * kNeedleLength);

    VSTGUI::CColor trailColor (0x55, 0x55, 0x88, 0xff);

    for (int i = 0; i < trailCount_; ++i)
    {
        // Read oldest to newest
        int idx = (trailIndex_ - trailCount_ + i + kTrailLength) % kTrailLength;
        float cents = trailCents_[idx];
        if (cents < -50.0f) cents = -50.0f;
        if (cents > 50.0f) cents = 50.0f;

        float angle = static_cast<float> (-M_PI / 2.0) + (cents / 50.0f) * kSweepHalfAngle;
        float nx = cx + needleLen * std::cos (angle);
        float ny = cy + needleLen * std::sin (angle);

        // Alpha fades with age: oldest ~0.03, newest ~0.25
        float ageFrac = static_cast<float> (i) / static_cast<float> (trailCount_ - 1);
        float alphaVal = 0.03f + ageFrac * 0.22f;

        ctx->saveGlobalState ();
        ctx->setGlobalAlpha (alphaVal);
        ctx->setFrameColor (trailColor);
        ctx->setLineWidth (1.5);
        VSTGUI::CLineStyle roundStyle (VSTGUI::CLineStyle::kLineCapRound);
        ctx->setLineStyle (roundStyle);
        ctx->drawLine (VSTGUI::CPoint (cx, cy), VSTGUI::CPoint (nx, ny));
        ctx->restoreGlobalState ();
    }
}

void TunerView::drawNeedleGlow (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
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
    float nx = cx + needleLen * std::cos (angle);
    float ny = cy + needleLen * std::sin (angle);

    // Accent color based on tuning accuracy
    float absCents = std::abs (smoothedCents_);
    VSTGUI::CColor glowColor;
    if (absCents < 5.0f)
        glowColor = VSTGUI::CColor (0x00, 0xe8, 0x7b, 0x26); // green at alpha 0.15
    else if (absCents < 15.0f)
        glowColor = VSTGUI::CColor (0xff, 0xb8, 0x30, 0x26); // amber at alpha 0.15
    else
        glowColor = VSTGUI::CColor (0xff, 0x44, 0x66, 0x26); // red at alpha 0.15

    ctx->setFrameColor (glowColor);
    ctx->setLineWidth (8.0);
    VSTGUI::CLineStyle roundStyle (VSTGUI::CLineStyle::kLineCapRound);
    ctx->setLineStyle (roundStyle);
    ctx->drawLine (VSTGUI::CPoint (cx, cy), VSTGUI::CPoint (nx, ny));
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
    float nx = cx + needleLen * std::cos (angle);
    float ny = cy + needleLen * std::sin (angle);

    VSTGUI::CLineStyle roundStyle (VSTGUI::CLineStyle::kLineCapRound);

    // Main needle: 3px white line with round caps
    ctx->setFrameColor (VSTGUI::CColor (0xe0, 0xe0, 0xff, 0xff));
    ctx->setLineWidth (3.0);
    ctx->setLineStyle (roundStyle);
    ctx->drawLine (VSTGUI::CPoint (cx, cy), VSTGUI::CPoint (nx, ny));

    // Center highlight: 1.5px brighter overlay
    ctx->setFrameColor (VSTGUI::CColor (0xf0, 0xf0, 0xff, 0xff));
    ctx->setLineWidth (1.5);
    ctx->setLineStyle (roundStyle);
    ctx->drawLine (VSTGUI::CPoint (cx, cy), VSTGUI::CPoint (nx, ny));

    // Pivot dot: small radial gradient dot
    auto* pivotPath = ctx->createGraphicsPath ();
    if (pivotPath)
    {
        float pivotR = 5.0f;
        VSTGUI::CRect pivotRect (cx - pivotR, cy - pivotR, cx + pivotR, cy + pivotR);
        pivotPath->addRect (pivotRect);
        auto pivotGrad = VSTGUI::owned (VSTGUI::CGradient::create (0.0, 1.0,
            VSTGUI::CColor (0xe0, 0xe0, 0xf0, 0xff),
            VSTGUI::CColor (0x60, 0x60, 0x80, 0xff)));
        ctx->fillRadialGradient (pivotPath, *pivotGrad,
            VSTGUI::CPoint (cx, cy), pivotR);
        pivotPath->forget ();

        // Stroke ring
        ctx->setFrameColor (VSTGUI::CColor (0x40, 0x40, 0x60, 0xff));
        ctx->setLineWidth (1.0);
        ctx->drawEllipse (pivotRect, VSTGUI::kDrawStroked);
    }
    else
    {
        // Fallback pivot
        VSTGUI::CRect pivot (cx - 5, cy - 5, cx + 5, cy + 5);
        ctx->setFillColor (VSTGUI::CColor (0xe0, 0xe0, 0xf0, 0xff));
        ctx->drawEllipse (pivot, VSTGUI::kDrawFilled);
        ctx->setFrameColor (VSTGUI::CColor (0x40, 0x40, 0x60, 0xff));
        ctx->setLineWidth (1.0);
        ctx->drawEllipse (pivot, VSTGUI::kDrawStroked);
    }
}

void TunerView::drawNoteBloom (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    if (bloomIntensity_ < 0.01f)
        return;

    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float noteY = static_cast<float> (r.top + r.getHeight () * kPivotY + 20);
    float bloomRadius = static_cast<float> (r.getHeight () * 0.12);

    auto* path = ctx->createGraphicsPath ();
    if (path)
    {
        VSTGUI::CRect bloomRect (cx - bloomRadius, noteY - bloomRadius * 0.6,
                                  cx + bloomRadius, noteY + bloomRadius * 0.6);
        path->addRect (bloomRect);

        auto innerAlpha = static_cast<uint8_t> (bloomIntensity_ * 0x18);
        auto gradient = VSTGUI::owned (VSTGUI::CGradient::create (0.0, 1.0,
            VSTGUI::CColor (0x00, 0xe8, 0x7b, innerAlpha),
            VSTGUI::CColor (0x00, 0xe8, 0x7b, 0x00)));
        ctx->fillRadialGradient (path, *gradient,
            VSTGUI::CPoint (cx, noteY), bloomRadius);
        path->forget ();
    }
    else
    {
        // Fallback: 3 concentric green ellipses
        for (int i = 2; i >= 0; --i)
        {
            float spread = 15.0f + static_cast<float> (i) * 15.0f;
            auto alpha = static_cast<uint8_t> (bloomIntensity_ * (18.0f - static_cast<float> (i) * 5.0f));
            VSTGUI::CColor glowColor (0x00, 0xe8, 0x7b, alpha);
            ctx->setFillColor (glowColor);
            VSTGUI::CRect ellipse (cx - spread, noteY - spread * 0.5, cx + spread, noteY + spread * 0.5);
            ctx->drawEllipse (ellipse, VSTGUI::kDrawFilled);
        }
    }
}

void TunerView::drawNoteDisplay (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float noteY = static_cast<float> (r.top + r.getHeight () * kPivotY + 20);

    bool active = confidence_ >= 0.1f && targetFrequency_ > 0.0f;

    // Text glow halo when bloom is active
    if (active && bloomIntensity_ > 0.3f)
    {
        auto haloFont = VSTGUI::makeOwned<VSTGUI::CFontDesc> ("Segoe UI", 54, VSTGUI::kBoldFace);
        ctx->setFont (haloFont);
        auto haloAlpha = static_cast<uint8_t> (bloomIntensity_ * 0.15f * 255.0f);
        ctx->setFontColor (VSTGUI::CColor (0x00, 0xe8, 0x7b, haloAlpha));
        VSTGUI::CRect haloRect (cx - 80, noteY, cx + 80, noteY + 56);
        ctx->drawString (VSTGUI::UTF8String (noteName_), haloRect, VSTGUI::kCenterText);
    }

    // Main note text
    auto noteFont = VSTGUI::makeOwned<VSTGUI::CFontDesc> ("Segoe UI", 52, VSTGUI::kBoldFace);
    ctx->setFont (noteFont);

    VSTGUI::CColor textColor;
    if (!active)
    {
        textColor = VSTGUI::CColor (0x66, 0x66, 0x88, 0xff);
    }
    else if (bloomIntensity_ > 0.5f)
    {
        // Interpolate from white to green
        float t = (bloomIntensity_ - 0.5f) * 2.0f; // 0..1
        t = std::min (t, 1.0f);
        auto red   = static_cast<uint8_t> (0xf0 * (1.0f - t));
        auto green = static_cast<uint8_t> (0xf0 + (0xe8 - 0xf0) * t);
        auto blue  = static_cast<uint8_t> (0xff * (1.0f - t) + 0x7b * t);
        textColor = VSTGUI::CColor (red, green, blue, 0xff);
    }
    else
    {
        textColor = VSTGUI::CColor (0xf0, 0xf0, 0xff, 0xff);
    }

    ctx->setFontColor (textColor);
    VSTGUI::CRect noteRect (cx - 80, noteY, cx + 80, noteY + 56);
    ctx->drawString (VSTGUI::UTF8String (noteName_), noteRect, VSTGUI::kCenterText);
}

void TunerView::drawInfoText (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r)
{
    auto monoFont = VSTGUI::makeOwned<VSTGUI::CFontDesc> ("Consolas", 13);
    ctx->setFont (monoFont);

    float cx = static_cast<float> (r.left + r.getWidth () / 2);
    float pivotAbsY = static_cast<float> (r.top + r.getHeight () * kPivotY);

    // Frequency display at pivot + 74px
    float hzY = pivotAbsY + 74.0f;
    char freqStr[32];
    if (targetFrequency_ > 0.0f)
        std::snprintf (freqStr, sizeof (freqStr), "%.1f Hz", targetFrequency_);
    else
        std::snprintf (freqStr, sizeof (freqStr), "--- Hz");

    ctx->setFontColor (VSTGUI::CColor (0xaa, 0xaa, 0xcc, 0xff));
    VSTGUI::CRect freqRect (cx - 80, hzY, cx + 80, hzY + 16);
    ctx->drawString (VSTGUI::UTF8String (freqStr), freqRect, VSTGUI::kCenterText);

    // Cents display at pivot + 90px, always show sign
    float centsY = pivotAbsY + 90.0f;
    char centsStr[32];
    if (targetFrequency_ > 0.0f)
    {
        if (targetCents_ >= 0.0f)
            std::snprintf (centsStr, sizeof (centsStr), "+%.1f ct", targetCents_);
        else
            std::snprintf (centsStr, sizeof (centsStr), "%.1f ct", targetCents_);
    }
    else
    {
        std::snprintf (centsStr, sizeof (centsStr), "--- ct");
    }

    VSTGUI::CRect centsRect (cx - 80, centsY, cx + 80, centsY + 16);
    ctx->drawString (VSTGUI::UTF8String (centsStr), centsRect, VSTGUI::kCenterText);
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
