#include "Control_Surface_Class.hpp"
#include <AH/Debug/Debug.hpp>
#include <AH/Hardware/ExtendedInputOutput/ExtendedIOElement.hpp>
#include <AH/Hardware/FilteredAnalog.hpp>
#include <MIDI_Constants/Control_Change.hpp>
#include <MIDI_Inputs/MIDIInputElement.hpp>
#include <MIDI_Outputs/Abstract/MIDIOutputElement.hpp>
#include <Selectors/Selector.hpp>

#include <AH/Arduino-Wrapper.h>

BEGIN_CS_NAMESPACE

using AH::ExtendedIOElement;

Control_Surface_ &Control_Surface_::getInstance() {
    static Control_Surface_ instance;
    return instance;
}

void Control_Surface_::begin() {
#if defined(ARDUINO) && defined(DEBUG_OUT)
    DEBUG_OUT.begin(AH::defaultBaudRate);
    delay(250);
#endif

    connectDefaultMIDI_Interface();

    FilteredAnalog<>::setupADC();
    ExtendedIOElement::beginAll();
    Updatable<MIDI_Interface>::beginAll();
    DisplayInterface::beginAll(); // initialize all displays
    MIDIInputElementNote::beginAll();
    MIDIInputElementKP::beginAll();
    MIDIInputElementCC::beginAll();
    MIDIInputElementPC::beginAll();
    MIDIInputElementCP::beginAll();
    MIDIInputElementPB::beginAll();
    MIDIInputElementSysEx::beginAll();
    Updatable<>::beginAll();
    Updatable<Potentiometer>::beginAll();
    Updatable<MotorFader>::beginAll();
    Updatable<Display>::beginAll();
    potentiometerTimer.begin();
    displayTimer.begin();
}

bool Control_Surface_::connectDefaultMIDI_Interface() {
    if (hasSinkPipe() || hasSourcePipe())
        return false;
    auto def = MIDI_Interface::getDefault();
    if (def == nullptr) {
        FATAL_ERROR(F("No default MIDI Interface"), 0xF123);
        return false;
    }
    *this << inpipe << *def;
    *this >> outpipe >> *def;
    return true;
}

void Control_Surface_::disconnectMIDI_Interfaces() {
    disconnectSinkPipes();
    disconnectSourcePipes();
}

void Control_Surface_::loop() {
    ExtendedIOElement::updateAllBufferedInputs();
    Updatable<>::updateAll();
    if (potentiometerTimer)
        Updatable<Potentiometer>::updateAll();
    updateMidiInput();
    updateInputs();
    if (displayTimer)
        updateDisplays();
    ExtendedIOElement::updateAllBufferedOutputs();
}

void Control_Surface_::updateMidiInput() {
    Updatable<MIDI_Interface>::updateAll();
}

void Control_Surface_::sendImpl(uint8_t header, uint8_t d1, uint8_t d2,
                                Cable cn) {
    this->sourceMIDItoPipe(ChannelMessage{header, d1, d2, cn});
}
void Control_Surface_::sendImpl(uint8_t header, uint8_t d1, Cable cn) {
    this->sourceMIDItoPipe(ChannelMessage{header, d1, 0x00, cn});
}
void Control_Surface_::sendImpl(const uint8_t *data, size_t length, Cable cn) {
    this->sourceMIDItoPipe(SysExMessage{data, length, cn});
}
void Control_Surface_::sendImpl(uint8_t rt, Cable cn) {
    this->sourceMIDItoPipe(RealTimeMessage{rt, cn});
}

void Control_Surface_::sinkMIDIfromPipe(ChannelMessage midimsg) {
#ifdef DEBUG_MIDI_PACKETS
    if (midimsg.hasTwoDataBytes())
        DEBUG(">>> " << hex << midimsg.header << ' ' << midimsg.data1 << ' '
                     << midimsg.data2 << " (" << midimsg.cable.getOneBased()
                     << ')' << dec);
    else
        DEBUG(">>> " << hex << midimsg.header << ' ' << midimsg.data1 << " ("
                     << midimsg.cable.getOneBased() << ')' << dec);
#endif

    // If the Channel Message callback exists, call it to see if we have to
    // continue handling it.
    if (channelMessageCallback && channelMessageCallback(midimsg))
        return;

    if (midimsg.getMessageType() == MIDIMessageType::CONTROL_CHANGE &&
        midimsg.getData1() == MIDI_CC::Reset_All_Controllers) {
        // Reset All Controllers
        DEBUG(F("Reset All Controllers"));
        MIDIInputElementCC::resetAll();
        MIDIInputElementCP::resetAll();
    } else if (midimsg.getMessageType() == MIDIMessageType::CONTROL_CHANGE &&
               midimsg.getData1() == MIDI_CC::All_Notes_Off) {
        // All Notes Off
        MIDIInputElementNote::resetAll();
    } else {
        switch (midimsg.getMessageType()) {
            case MIDIMessageType::NOTE_OFF: // fallthrough
            case MIDIMessageType::NOTE_ON:
                DEBUGFN(F("Updating Note elements with new MIDI "
                          "message."));
                MIDIInputElementNote::updateAllWith(midimsg);
                break;
            case MIDIMessageType::KEY_PRESSURE:
                DEBUGFN(F("Updating Key Pressure elements with new MIDI "
                          "message."));
                MIDIInputElementKP::updateAllWith(midimsg);
                break;
            case MIDIMessageType::CONTROL_CHANGE:
                DEBUGFN(F("Updating CC elements with new MIDI "
                          "message."));
                MIDIInputElementCC::updateAllWith(midimsg);
                break;
            case MIDIMessageType::PROGRAM_CHANGE:
                DEBUGFN(F("Updating Program Change elements with new MIDI "
                          "message."));
                MIDIInputElementPC::updateAllWith(midimsg);
                break;
            case MIDIMessageType::CHANNEL_PRESSURE:
                DEBUGFN(F("Updating Channel Pressure elements with new MIDI "
                          "message."));
                MIDIInputElementCP::updateAllWith(midimsg);
                break;
            case MIDIMessageType::PITCH_BEND:
                // Channel Pressure
                DEBUGFN(F("Updating Pitch Bend elements with new MIDI "
                          "message."));
                MIDIInputElementPB::updateAllWith(midimsg);
                break;

            // These MIDI types are not channel messages, so aren't handled here
            // LCOV_EXCL_START
            case MIDIMessageType::SYSEX_START: break;
            case MIDIMessageType::MTC_QUARTER_FRAME: break;
            case MIDIMessageType::SONG_POSITION_POINTER: break;
            case MIDIMessageType::SONG_SELECT: break;
            case MIDIMessageType::UNDEFINED_SYSCOMMON_1: break;
            case MIDIMessageType::UNDEFINED_SYSCOMMON_2: break;
            case MIDIMessageType::TUNE_REQUEST: break;
            case MIDIMessageType::SYSEX_END: break;
            case MIDIMessageType::TIMING_CLOCK: break;
            case MIDIMessageType::UNDEFINED_REALTIME_1: break;
            case MIDIMessageType::START: break;
            case MIDIMessageType::CONTINUE: break;
            case MIDIMessageType::STOP: break;
            case MIDIMessageType::UNDEFINED_REALTIME_2: break;
            case MIDIMessageType::ACTIVE_SENSING: break;
            case MIDIMessageType::RESET: break;
            default:
                break;
                // LCOV_EXCL_STOP
        }
    }
}

void Control_Surface_::sinkMIDIfromPipe(SysExMessage msg) {
#ifdef DEBUG_MIDI_PACKETS
    const uint8_t *data = msg.data;
    size_t len = msg.length;
    DEBUG_OUT << ">>> " << hex;
    for (size_t i = 0; i < len; i++)
        DEBUG_OUT << data[i] << ' ';
    DEBUG_OUT << " (" << msg.cable << ')' << dec << endl;
#endif
    // If the SysEx Message callback exists, call it to see if we have to
    // continue handling it.
    if (sysExMessageCallback && sysExMessageCallback(msg))
        return;
    MIDIInputElementSysEx::updateAllWith(msg);
}

void Control_Surface_::sinkMIDIfromPipe(RealTimeMessage rtMessage) {
#ifdef DEBUG_MIDI_PACKETS
    DEBUG(">>> " << hex << rtMessage.message << " ("
                 << rtMessage.cable.getOneBased() << ')' << dec);
#endif

    // If the Real-Time Message callback exists, call it to see if we have to
    // continue handling it.
    if (realTimeMessageCallback && realTimeMessageCallback(rtMessage))
        return;
}

void Control_Surface_::updateInputs() {
    MIDIInputElementNote::updateAll();
    MIDIInputElementKP::updateAll();
    MIDIInputElementCC::updateAll();
    MIDIInputElementPC::updateAll();
    MIDIInputElementCP::updateAll();
    MIDIInputElementPB::updateAll();
    MIDIInputElementSysEx::updateAll();
}

void Control_Surface_::updateDisplays() {
    auto &allElements = DisplayElement::getAll();
    auto it = allElements.begin();
    auto end = allElements.end();
    if (it == end)
        return;
    auto prevIt = it;
    auto previousDisplay = &prevIt->getDisplay();
    bool dirty = false;
    // Loop over all display elements
    while (true) {
        dirty |= it->getDirty();
        ++it;
        // If this is the first element on another display
        if (it == end || &it->getDisplay() != previousDisplay) {
            // If there was at least one element on the previous display that
            // has to be redrawn
            if (dirty) {
                // Clear the display
                previousDisplay->clearAndDrawBackground();
                // Update all elements on that display
                for (auto drawIt = prevIt; drawIt != it; ++drawIt)
                    drawIt->draw();
                // Write the buffer to the display
                previousDisplay->display();
            }
            if (it == end)
                break;
            prevIt = it;
            previousDisplay = &it->getDisplay();
            dirty = false;
        }
    }
}

Control_Surface_ &Control_Surface = Control_Surface_::getInstance();

END_CS_NAMESPACE