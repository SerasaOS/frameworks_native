/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// clang-format off
#include "../Macros.h"
// clang-format on

#include "CursorInputMapper.h"

#include "CursorButtonAccumulator.h"
#include "CursorScrollAccumulator.h"
#include "PointerControllerInterface.h"
#include "TouchCursorInputMapperCommon.h"

#include "input/PrintTools.h"

namespace android {

// The default velocity control parameters that has no effect.
static const VelocityControlParameters FLAT_VELOCITY_CONTROL_PARAMS{};

// --- CursorMotionAccumulator ---

CursorMotionAccumulator::CursorMotionAccumulator() {
    clearRelativeAxes();
}

void CursorMotionAccumulator::reset(InputDeviceContext& deviceContext) {
    clearRelativeAxes();
}

void CursorMotionAccumulator::clearRelativeAxes() {
    mRelX = 0;
    mRelY = 0;
}

void CursorMotionAccumulator::process(const RawEvent* rawEvent) {
    if (rawEvent->type == EV_REL) {
        switch (rawEvent->code) {
            case REL_X:
                mRelX = rawEvent->value;
                break;
            case REL_Y:
                mRelY = rawEvent->value;
                break;
        }
    }
}

void CursorMotionAccumulator::finishSync() {
    clearRelativeAxes();
}

// --- CursorPositionAccumulator ---

CursorPositionAccumulator::CursorPositionAccumulator() : mMinAbsX(0), mMinAbsY(0), mMaxAbsX(0), mMaxAbsY(0), mHasAbsX(false), mHasAbsY(false), mMoved(false) {
    clearPos();
}

void CursorPositionAccumulator::reset(InputDeviceContext& deviceContext) {
    clearPos();
}

void CursorPositionAccumulator::configure(InputMapper* im, InputDeviceContext& deviceContext) {
    mHasAbsX = deviceContext.hasAbsoluteAxis(ABS_X);
    mHasAbsY = deviceContext.hasAbsoluteAxis(ABS_Y);
    if (hasAbsX()) {
        RawAbsoluteAxisInfo axisInfo;
        im->getAbsoluteAxisInfo(ABS_X, &axisInfo);
        mMinAbsX = axisInfo.minValue;
        mMaxAbsX = axisInfo.maxValue;
    }
    if (hasAbsY()) {
        RawAbsoluteAxisInfo axisInfo;
        im->getAbsoluteAxisInfo(ABS_Y, &axisInfo);
        mMinAbsY = axisInfo.minValue;
        mMaxAbsY = axisInfo.maxValue;
    }
}

void CursorPositionAccumulator::clearPos() {
    mDeltaX = 0;
    mDeltaY = 0;
}

void CursorPositionAccumulator::process(const RawEvent* rawEvent) {
    if (rawEvent->type == EV_ABS) {
        int32_t newValue = rawEvent->value;
        switch (rawEvent->code) {
            case ABS_X:
                newValue -= mMinAbsX;
                if (mMoved) {
                    mDeltaX = newValue - mX;
                } else {
                    mMoved = true;
                }
                mX = newValue;
                break;
            case ABS_Y:
                newValue -= mMinAbsY;
                if (mMoved) {
                    mDeltaY = newValue - mY;
                } else {
                    mMoved = true;
                }
                mY = newValue;
                break;
        }
    }
}

void CursorPositionAccumulator::finishSync() {
    clearPos();
}

// --- CursorInputMapper ---

CursorInputMapper::CursorInputMapper(InputDeviceContext& deviceContext)
      : InputMapper(deviceContext) {}

CursorInputMapper::~CursorInputMapper() {}

uint32_t CursorInputMapper::getSources() const {
    return mSource;
}

void CursorInputMapper::populateDeviceInfo(InputDeviceInfo* info) {
    InputMapper::populateDeviceInfo(info);

    if (mParameters.mode == Parameters::Mode::POINTER) {
        float minX, minY, maxX, maxY;
        if (mPointerController->getBounds(&minX, &minY, &maxX, &maxY)) {
            info->addMotionRange(AMOTION_EVENT_AXIS_X, mSource, minX, maxX, 0.0f, 0.0f, 0.0f);
            info->addMotionRange(AMOTION_EVENT_AXIS_Y, mSource, minY, maxY, 0.0f, 0.0f, 0.0f);
        }
    } else {
        info->addMotionRange(AMOTION_EVENT_AXIS_X, mSource, -1.0f, 1.0f, 0.0f, mXScale, 0.0f);
        info->addMotionRange(AMOTION_EVENT_AXIS_Y, mSource, -1.0f, 1.0f, 0.0f, mYScale, 0.0f);
        info->addMotionRange(AMOTION_EVENT_AXIS_RELATIVE_X, mSource, -1.0f, 1.0f, 0.0f, mXScale,
                             0.0f);
        info->addMotionRange(AMOTION_EVENT_AXIS_RELATIVE_Y, mSource, -1.0f, 1.0f, 0.0f, mYScale,
                             0.0f);
    }
    info->addMotionRange(AMOTION_EVENT_AXIS_PRESSURE, mSource, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);

    if (mCursorScrollAccumulator.haveRelativeVWheel()) {
        info->addMotionRange(AMOTION_EVENT_AXIS_VSCROLL, mSource, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    }
    if (mCursorScrollAccumulator.haveRelativeHWheel()) {
        info->addMotionRange(AMOTION_EVENT_AXIS_HSCROLL, mSource, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    }
}

void CursorInputMapper::dump(std::string& dump) {
    dump += INDENT2 "Cursor Input Mapper:\n";
    dumpParameters(dump);
    dump += StringPrintf(INDENT3 "XScale: %0.3f\n", mXScale);
    dump += StringPrintf(INDENT3 "YScale: %0.3f\n", mYScale);
    dump += StringPrintf(INDENT3 "XPrecision: %0.3f\n", mXPrecision);
    dump += StringPrintf(INDENT3 "YPrecision: %0.3f\n", mYPrecision);
    dump += StringPrintf(INDENT3 "AbsXScale: %0.3f\n", mAbsXScale);
    dump += StringPrintf(INDENT3 "AbsYScale: %0.3f\n", mAbsYScale);
    dump += StringPrintf(INDENT3 "AbsXPrecision: %0.3f\n", mAbsXPrecision);
    dump += StringPrintf(INDENT3 "AbsYPrecision: %0.3f\n", mAbsYPrecision);
    dump += StringPrintf(INDENT3 "HaveVWheel: %s\n",
                         toString(mCursorScrollAccumulator.haveRelativeVWheel()));
    dump += StringPrintf(INDENT3 "HaveHWheel: %s\n",
                         toString(mCursorScrollAccumulator.haveRelativeHWheel()));
    dump += StringPrintf(INDENT3 "VWheelScale: %0.3f\n", mVWheelScale);
    dump += StringPrintf(INDENT3 "HWheelScale: %0.3f\n", mHWheelScale);
    dump += StringPrintf(INDENT3 "DisplayId: %s\n", toString(mDisplayId).c_str());
    dump += StringPrintf(INDENT3 "Orientation: %d\n", mOrientation);
    dump += StringPrintf(INDENT3 "ButtonState: 0x%08x\n", mButtonState);
    dump += StringPrintf(INDENT3 "Down: %s\n", toString(isPointerDown(mButtonState)));
    dump += StringPrintf(INDENT3 "DownTime: %" PRId64 "\n", mDownTime);
}

void CursorInputMapper::configure(nsecs_t when, const InputReaderConfiguration* config,
                                  uint32_t changes) {
    InputMapper::configure(when, config, changes);

    if (!changes) { // first time only
        mCursorScrollAccumulator.configure(getDeviceContext());
        mCursorPositionAccumulator.configure(this, getDeviceContext());

        // Configure basic parameters.
        configureParameters();

        // Configure device mode.
        switch (mParameters.mode) {
            case Parameters::Mode::POINTER_RELATIVE:
                // Should not happen during first time configuration.
                ALOGE("Cannot start a device in MODE_POINTER_RELATIVE, starting in MODE_POINTER");
                mParameters.mode = Parameters::Mode::POINTER;
                [[fallthrough]];
            case Parameters::Mode::POINTER:
                mSource = AINPUT_SOURCE_MOUSE;
                mXPrecision = 1.0f;
                mYPrecision = 1.0f;
                mXScale = 1.0f;
                mYScale = 1.0f;
                mPointerController = getContext()->getPointerController(getDeviceId());
                break;
            case Parameters::Mode::NAVIGATION:
                mSource = AINPUT_SOURCE_TRACKBALL;
                mXPrecision = TRACKBALL_MOVEMENT_THRESHOLD;
                mYPrecision = TRACKBALL_MOVEMENT_THRESHOLD;
                mXScale = 1.0f / TRACKBALL_MOVEMENT_THRESHOLD;
                mYScale = 1.0f / TRACKBALL_MOVEMENT_THRESHOLD;
                break;
        }

        mVWheelScale = 1.0f;
        mHWheelScale = 1.0f;
        mAbsXPrecision = 1.0f;
        mAbsYPrecision = 1.0f;
        mAbsXScale = 1.0f;
        mAbsYScale = 1.0f;
    }

    const bool configurePointerCapture = mParameters.mode != Parameters::Mode::NAVIGATION &&
            ((!changes && config->pointerCaptureRequest.enable) ||
             (changes & InputReaderConfiguration::CHANGE_POINTER_CAPTURE));
    if (configurePointerCapture) {
        if (config->pointerCaptureRequest.enable) {
            if (mParameters.mode == Parameters::Mode::POINTER) {
                mParameters.mode = Parameters::Mode::POINTER_RELATIVE;
                mSource = AINPUT_SOURCE_MOUSE_RELATIVE;
                // Keep PointerController around in order to preserve the pointer position.
                mPointerController->fade(PointerControllerInterface::Transition::IMMEDIATE);
            } else {
                ALOGE("Cannot request pointer capture, device is not in MODE_POINTER");
            }
        } else {
            if (mParameters.mode == Parameters::Mode::POINTER_RELATIVE) {
                mParameters.mode = Parameters::Mode::POINTER;
                mSource = AINPUT_SOURCE_MOUSE;
            } else {
                ALOGE("Cannot release pointer capture, device is not in MODE_POINTER_RELATIVE");
            }
        }
        bumpGeneration();
        if (changes) {
            NotifyDeviceResetArgs args(getContext()->getNextId(), when, getDeviceId());
            getListener().notifyDeviceReset(&args);
        }
    }

    if (!changes || (changes & InputReaderConfiguration::CHANGE_POINTER_SPEED) ||
        configurePointerCapture) {
        if (mParameters.mode == Parameters::Mode::POINTER_RELATIVE) {
            // Disable any acceleration or scaling for the pointer when Pointer Capture is enabled.
            mPointerVelocityControl.setParameters(FLAT_VELOCITY_CONTROL_PARAMS);
            mWheelXVelocityControl.setParameters(FLAT_VELOCITY_CONTROL_PARAMS);
            mWheelYVelocityControl.setParameters(FLAT_VELOCITY_CONTROL_PARAMS);
        } else {
            mPointerVelocityControl.setParameters(config->pointerVelocityControlParameters);
            mWheelXVelocityControl.setParameters(config->wheelVelocityControlParameters);
            mWheelYVelocityControl.setParameters(config->wheelVelocityControlParameters);
        }
    }

    if (!changes || (changes & InputReaderConfiguration::CHANGE_FORCE_MOUSE_AS_TOUCH) ||
        configurePointerCapture) {
        if (mParameters.mode == Parameters::Mode::POINTER_RELATIVE) {
            // Disable touch emulation for the pointer when Pointer Capture is enabled.
            mSource = AINPUT_SOURCE_MOUSE_RELATIVE;
        } else if (config->forceMouseAsTouch) {
            mSource = AINPUT_SOURCE_TOUCHSCREEN;
        } else {
            mSource = AINPUT_SOURCE_MOUSE;
        }
    }

    if (!changes || (changes & InputReaderConfiguration::CHANGE_DISPLAY_INFO) ||
        configurePointerCapture) {
        const bool isPointer = mParameters.mode == Parameters::Mode::POINTER;

        mDisplayId = ADISPLAY_ID_NONE;
        if (auto viewport = mDeviceContext.getAssociatedViewport(); viewport) {
            // This InputDevice is associated with a viewport.
            // Only generate events for the associated display.
            const bool mismatchedPointerDisplay =
                    isPointer && (viewport->displayId != mPointerController->getDisplayId());
            mDisplayId = mismatchedPointerDisplay ? std::nullopt
                                                  : std::make_optional(viewport->displayId);
        } else if (isPointer) {
            // The InputDevice is not associated with a viewport, but it controls the mouse pointer.
            mDisplayId = mPointerController->getDisplayId();
        }

        mOrientation = DISPLAY_ORIENTATION_0;
        const bool isOrientedDevice =
                (mParameters.orientationAware && mParameters.hasAssociatedDisplay);
        // InputReader works in the un-rotated display coordinate space, so we don't need to do
        // anything if the device is already orientation-aware. If the device is not
        // orientation-aware, then we need to apply the inverse rotation of the display so that
        // when the display rotation is applied later as a part of the per-window transform, we
        // get the expected screen coordinates. When pointer capture is enabled, we do not apply any
        // rotations and report values directly from the input device.
        if (!isOrientedDevice && mDisplayId &&
            mParameters.mode != Parameters::Mode::POINTER_RELATIVE) {
            if (auto viewport = config->getDisplayViewportById(*mDisplayId); viewport) {
                mOrientation = getInverseRotation(viewport->orientation);
            }
        }

        if (mDisplayId && mCursorPositionAccumulator.isSupported()) {
            if (auto viewport = config->getDisplayViewportById(*mDisplayId); viewport) {
                mAbsXScale = float(viewport->physicalRight - viewport->physicalLeft) / mCursorPositionAccumulator.getSpanAbsX();
                mAbsYScale = float(viewport->physicalBottom - viewport->physicalTop) / mCursorPositionAccumulator.getSpanAbsY();
                mAbsXPrecision = 1.0f / mAbsXScale;
                mAbsYPrecision = 1.0f / mAbsYScale;
            }
        }

        bumpGeneration();
    }
}

void CursorInputMapper::configureParameters() {
    mParameters.mode = Parameters::Mode::POINTER;
    String8 cursorModeString;
    if (getDeviceContext().getConfiguration().tryGetProperty(String8("cursor.mode"),
                                                             cursorModeString)) {
        if (cursorModeString == "navigation") {
            mParameters.mode = Parameters::Mode::NAVIGATION;
        } else if (cursorModeString != "pointer" && cursorModeString != "default") {
            ALOGW("Invalid value for cursor.mode: '%s'", cursorModeString.string());
        }
    }

    mParameters.orientationAware = false;
    getDeviceContext().getConfiguration().tryGetProperty(String8("cursor.orientationAware"),
                                                         mParameters.orientationAware);

    mParameters.hasAssociatedDisplay = false;
    if (mParameters.mode == Parameters::Mode::POINTER || mParameters.orientationAware) {
        mParameters.hasAssociatedDisplay = true;
    }
}

void CursorInputMapper::dumpParameters(std::string& dump) {
    dump += INDENT3 "Parameters:\n";
    dump += StringPrintf(INDENT4 "HasAssociatedDisplay: %s\n",
                         toString(mParameters.hasAssociatedDisplay));
    dump += StringPrintf(INDENT4 "Mode: %s\n", ftl::enum_string(mParameters.mode).c_str());
    dump += StringPrintf(INDENT4 "OrientationAware: %s\n", toString(mParameters.orientationAware));
}

void CursorInputMapper::reset(nsecs_t when) {
    mButtonState = 0;
    mDownTime = 0;

    mPointerVelocityControl.reset();
    mWheelXVelocityControl.reset();
    mWheelYVelocityControl.reset();

    mCursorButtonAccumulator.reset(getDeviceContext());
    mCursorMotionAccumulator.reset(getDeviceContext());
    mCursorPositionAccumulator.reset(getDeviceContext());
    mCursorScrollAccumulator.reset(getDeviceContext());

    InputMapper::reset(when);
}

void CursorInputMapper::rotateAbsolute(int32_t orientation, float* absX, float* absY) {
    float temp = *absX;
    switch (orientation) {
        case DISPLAY_ORIENTATION_90:
            *absX = *absY;
            *absY = (mCursorPositionAccumulator.getSpanAbsX() * mXScale) - temp;
            break;

        case DISPLAY_ORIENTATION_180:
            *absX = (mCursorPositionAccumulator.getSpanAbsX() * mXScale) - *absX;
            *absY = (mCursorPositionAccumulator.getSpanAbsY() * mYScale) - *absY;
            break;

        case DISPLAY_ORIENTATION_270:
            *absX = (mCursorPositionAccumulator.getSpanAbsY() * mYScale) - *absY;
            *absY = temp;
            break;
    }
}

void CursorInputMapper::process(const RawEvent* rawEvent) {
    mCursorButtonAccumulator.process(rawEvent);
    mCursorMotionAccumulator.process(rawEvent);
    mCursorPositionAccumulator.process(rawEvent);
    mCursorScrollAccumulator.process(rawEvent);

    if (rawEvent->type == EV_SYN && rawEvent->code == SYN_REPORT) {
        sync(rawEvent->when, rawEvent->readTime);
    }
}

void CursorInputMapper::sync(nsecs_t when, nsecs_t readTime) {
    if (!mDisplayId) {
        // Ignore events when there is no target display configured.
        return;
    }

    int32_t lastButtonState = mButtonState;
    int32_t currentButtonState = mCursorButtonAccumulator.getButtonState();
    mButtonState = currentButtonState;

    bool wasDown = isPointerDown(lastButtonState);
    bool down = isPointerDown(currentButtonState);
    bool downChanged;
    if (!wasDown && down) {
        mDownTime = when;
        downChanged = true;
    } else if (wasDown && !down) {
        downChanged = true;
    } else {
        downChanged = false;
    }
    nsecs_t downTime = mDownTime;
    bool buttonsChanged = currentButtonState != lastButtonState;
    int32_t buttonsPressed = currentButtonState & ~lastButtonState;
    int32_t buttonsReleased = lastButtonState & ~currentButtonState;

    float deltaX = mCursorMotionAccumulator.getRelativeX() * mXScale;
    float deltaY = mCursorMotionAccumulator.getRelativeY() * mYScale;
    bool moved = deltaX != 0 || deltaY != 0;

    // Rotate delta according to orientation.
    rotateDelta(mOrientation, &deltaX, &deltaY);

    float absX = mCursorPositionAccumulator.getX() * mAbsXScale;
    float absY = mCursorPositionAccumulator.getY() * mAbsYScale;
    bool movedAbs = mCursorPositionAccumulator.hasMoved() && absX >= 0 && absY >= 0;

    // Rotate absolute according to orientation.
    rotateAbsolute(mOrientation, &absX, &absY);

    if (!moved && movedAbs) {
        // Delta emulation for cursor grab.
        deltaX = mCursorPositionAccumulator.getDeltaX() * mXScale;
        deltaY = mCursorPositionAccumulator.getDeltaY() * mYScale;

        // Rotate delta according to orientation.
        rotateDelta(mOrientation, &deltaX, &deltaY);
    }

    // Move the pointer.
    PointerProperties pointerProperties;
    pointerProperties.clear();
    pointerProperties.id = 0;
    pointerProperties.toolType = AMOTION_EVENT_TOOL_TYPE_MOUSE;

    PointerCoords pointerCoords;
    pointerCoords.clear();

    float vscroll = mCursorScrollAccumulator.getRelativeVWheel();
    float hscroll = mCursorScrollAccumulator.getRelativeHWheel();
    bool scrolled = vscroll != 0 || hscroll != 0;

    mWheelYVelocityControl.move(when, nullptr, &vscroll);
    mWheelXVelocityControl.move(when, &hscroll, nullptr);

    mPointerVelocityControl.move(when, &deltaX, &deltaY);

    float xCursorPosition = AMOTION_EVENT_INVALID_CURSOR_POSITION;
    float yCursorPosition = AMOTION_EVENT_INVALID_CURSOR_POSITION;
    if (mSource == AINPUT_SOURCE_MOUSE || mSource == AINPUT_SOURCE_TOUCHSCREEN) {
        if (moved || movedAbs || scrolled || buttonsChanged) {
            mPointerController->setPresentation(PointerControllerInterface::Presentation::POINTER);

            if (moved) {
                mPointerController->move(deltaX, deltaY);
            } else if (movedAbs) {
                mPointerController->getPosition(&deltaX, &deltaY);
                mPointerController->setPosition(absX, absY);
                // Report correct delta values (not accelerated) by parsing difference
                deltaX = absX - deltaX; deltaY = absY - deltaY;
            }

            if (buttonsChanged) {
                mPointerController->setButtonState(currentButtonState);
            }

            mPointerController->unfade(PointerControllerInterface::Transition::IMMEDIATE);
        }

        mPointerController->getPosition(&xCursorPosition, &yCursorPosition);

        pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_X, xCursorPosition);
        pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_Y, yCursorPosition);
        pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_RELATIVE_X, deltaX);
        pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_RELATIVE_Y, deltaY);
    } else {
        // Pointer capture and navigation modes
        pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_X, deltaX);
        pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_Y, deltaY);
        pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_RELATIVE_X, deltaX);
        pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_RELATIVE_Y, deltaY);
    }

    pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_PRESSURE, down ? 1.0f : 0.0f);

    // Moving an external trackball or mouse should wake the device.
    // We don't do this for internal cursor devices to prevent them from waking up
    // the device in your pocket.
    // TODO: Use the input device configuration to control this behavior more finely.
    uint32_t policyFlags = 0;
    if ((buttonsPressed || moved || movedAbs || scrolled) && getDeviceContext().isExternal()) {
        policyFlags |= POLICY_FLAG_WAKE;
    }

    // Synthesize key down from buttons if needed.
    synthesizeButtonKeys(getContext(), AKEY_EVENT_ACTION_DOWN, when, readTime, getDeviceId(),
                         mSource, *mDisplayId, policyFlags, lastButtonState, currentButtonState);

    // Send motion event.
    if (downChanged || moved || movedAbs || scrolled || buttonsChanged) {
        int32_t metaState = getContext()->getGlobalMetaState();
        int32_t buttonState = lastButtonState;
        float xPrecision = mXPrecision;
        float yPrecision = mYPrecision;
        if (!moved && movedAbs) {
            xPrecision = mAbsXPrecision;
            yPrecision = mAbsYPrecision;
        }
        int32_t motionEventAction;
        if (downChanged) {
            motionEventAction = down ? AMOTION_EVENT_ACTION_DOWN : AMOTION_EVENT_ACTION_UP;
        } else if (down || (mSource != AINPUT_SOURCE_MOUSE && mSource != AINPUT_SOURCE_TOUCHSCREEN)) {
            motionEventAction = AMOTION_EVENT_ACTION_MOVE;
        } else {
            motionEventAction = AMOTION_EVENT_ACTION_HOVER_MOVE;
        }

        if (buttonsReleased) {
            BitSet32 released(buttonsReleased);
            while (!released.isEmpty()) {
                int32_t actionButton = BitSet32::valueForBit(released.clearFirstMarkedBit());
                buttonState &= ~actionButton;
                NotifyMotionArgs releaseArgs(getContext()->getNextId(), when, readTime,
                                             getDeviceId(), mSource, *mDisplayId, policyFlags,
                                             AMOTION_EVENT_ACTION_BUTTON_RELEASE, actionButton, 0,
                                             metaState, buttonState, MotionClassification::NONE,
                                             AMOTION_EVENT_EDGE_FLAG_NONE, 1, &pointerProperties,
                                             &pointerCoords, xPrecision, yPrecision,
                                             xCursorPosition, yCursorPosition, downTime,
                                             /* videoFrames */ {});
                getListener().notifyMotion(&releaseArgs);
            }
        }

        NotifyMotionArgs args(getContext()->getNextId(), when, readTime, getDeviceId(), mSource,
                              *mDisplayId, policyFlags, motionEventAction, 0, 0, metaState,
                              currentButtonState, MotionClassification::NONE,
                              AMOTION_EVENT_EDGE_FLAG_NONE, 1, &pointerProperties, &pointerCoords,
                              xPrecision, yPrecision, xCursorPosition, yCursorPosition, downTime,
                              /* videoFrames */ {});
        getListener().notifyMotion(&args);

        if (buttonsPressed) {
            BitSet32 pressed(buttonsPressed);
            while (!pressed.isEmpty()) {
                int32_t actionButton = BitSet32::valueForBit(pressed.clearFirstMarkedBit());
                buttonState |= actionButton;
                NotifyMotionArgs pressArgs(getContext()->getNextId(), when, readTime, getDeviceId(),
                                           mSource, *mDisplayId, policyFlags,
                                           AMOTION_EVENT_ACTION_BUTTON_PRESS, actionButton, 0,
                                           metaState, buttonState, MotionClassification::NONE,
                                           AMOTION_EVENT_EDGE_FLAG_NONE, 1, &pointerProperties,
                                           &pointerCoords, xPrecision, yPrecision,
                                           xCursorPosition, yCursorPosition, downTime,
                                           /* videoFrames */ {});
                getListener().notifyMotion(&pressArgs);
            }
        }

        ALOG_ASSERT(buttonState == currentButtonState);

        // Send hover move after UP to tell the application that the mouse is hovering now.
        if (motionEventAction == AMOTION_EVENT_ACTION_UP && (mSource == AINPUT_SOURCE_MOUSE || mSource == AINPUT_SOURCE_TOUCHSCREEN)) {
            NotifyMotionArgs hoverArgs(getContext()->getNextId(), when, readTime, getDeviceId(),
                                       mSource, *mDisplayId, policyFlags,
                                       AMOTION_EVENT_ACTION_HOVER_MOVE, 0, 0, metaState,
                                       currentButtonState, MotionClassification::NONE,
                                       AMOTION_EVENT_EDGE_FLAG_NONE, 1, &pointerProperties,
                                       &pointerCoords, xPrecision, yPrecision, xCursorPosition,
                                       yCursorPosition, downTime, /* videoFrames */ {});
            getListener().notifyMotion(&hoverArgs);
        }

        // Send scroll events.
        if (scrolled) {
            pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_VSCROLL, vscroll);
            pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_HSCROLL, hscroll);

            NotifyMotionArgs scrollArgs(getContext()->getNextId(), when, readTime, getDeviceId(),
                                        mSource, *mDisplayId, policyFlags,
                                        AMOTION_EVENT_ACTION_SCROLL, 0, 0, metaState,
                                        currentButtonState, MotionClassification::NONE,
                                        AMOTION_EVENT_EDGE_FLAG_NONE, 1, &pointerProperties,
                                        &pointerCoords, xPrecision, yPrecision, xCursorPosition,
                                        yCursorPosition, downTime, /* videoFrames */ {});
            getListener().notifyMotion(&scrollArgs);
        }
    }

    // Synthesize key up from buttons if needed.
    synthesizeButtonKeys(getContext(), AKEY_EVENT_ACTION_UP, when, readTime, getDeviceId(), mSource,
                         *mDisplayId, policyFlags, lastButtonState, currentButtonState);

    mCursorMotionAccumulator.finishSync();
    mCursorPositionAccumulator.finishSync();
    mCursorScrollAccumulator.finishSync();
}

int32_t CursorInputMapper::getScanCodeState(uint32_t sourceMask, int32_t scanCode) {
    if (scanCode >= BTN_MOUSE && scanCode < BTN_JOYSTICK) {
        return getDeviceContext().getScanCodeState(scanCode);
    } else {
        return AKEY_STATE_UNKNOWN;
    }
}

std::optional<int32_t> CursorInputMapper::getAssociatedDisplayId() {
    return mDisplayId;
}

} // namespace android
