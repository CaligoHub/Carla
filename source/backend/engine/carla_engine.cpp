/*
 * Carla Engine
 * Copyright (C) 2012-2013 Filipe Coelho <falktx@falktx.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the GPL.txt file
 */

#include "carla_engine_internal.hpp"
#include "carla_backend_utils.hpp"
#include "carla_midi.h"

#include <QtCore/QFile>
#include <QtCore/QTextStream>

CARLA_BACKEND_START_NAMESPACE

// -------------------------------------------------------------------------------------------------------------------
// Carla Engine port (Abstract)

CarlaEnginePort::CarlaEnginePort(const bool isInput, const ProcessMode processMode)
    : kIsInput(isInput),
      kProcessMode(processMode)
{
    qDebug("CarlaEnginePort::CarlaEnginePort(%s, %s)", bool2str(isInput), ProcessMode2Str(processMode));
}

CarlaEnginePort::~CarlaEnginePort()
{
    qDebug("CarlaEnginePort::~CarlaEnginePort()");
}

// -------------------------------------------------------------------------------------------------------------------
// Carla Engine Audio port

CarlaEngineAudioPort::CarlaEngineAudioPort(const bool isInput, const ProcessMode processMode)
    : CarlaEnginePort(isInput, processMode),
      fBuffer(nullptr)
{
    qDebug("CarlaEngineAudioPort::CarlaEngineAudioPort(%s, %s)", bool2str(isInput), ProcessMode2Str(processMode));

    if (kProcessMode == PROCESS_MODE_PATCHBAY)
        fBuffer = new float[PATCHBAY_BUFFER_SIZE];
}

CarlaEngineAudioPort::~CarlaEngineAudioPort()
{
    qDebug("CarlaEngineAudioPort::~CarlaEngineAudioPort()");

    if (kProcessMode == PROCESS_MODE_PATCHBAY)
    {
        CARLA_ASSERT(fBuffer != nullptr);

        if (fBuffer != nullptr)
            delete[] fBuffer;
    }
}

void CarlaEngineAudioPort::initBuffer(CarlaEngine* const)
{
    if (kProcessMode == PROCESS_MODE_PATCHBAY && ! kIsInput)
        carla_zeroFloat(fBuffer, PATCHBAY_BUFFER_SIZE);
}

// -------------------------------------------------------------------------------------------------------------------
// Carla Engine Event port

static const EngineEvent kFallbackEngineEvent;

CarlaEngineEventPort::CarlaEngineEventPort(const bool isInput, const ProcessMode processMode)
    : CarlaEnginePort(isInput, processMode),
      kMaxEventCount(processMode == PROCESS_MODE_CONTINUOUS_RACK ? RACK_EVENT_COUNT : PATCHBAY_EVENT_COUNT),
      fBuffer(nullptr)
{
    qDebug("CarlaEngineEventPort::CarlaEngineEventPort(%s, %s)", bool2str(isInput), ProcessMode2Str(processMode));

    if (kProcessMode == PROCESS_MODE_PATCHBAY)
        fBuffer = new EngineEvent[PATCHBAY_EVENT_COUNT];
}

CarlaEngineEventPort::~CarlaEngineEventPort()
{
    qDebug("CarlaEngineEventPort::~CarlaEngineEventPort()");

    if (kProcessMode == PROCESS_MODE_PATCHBAY)
    {
        CARLA_ASSERT(fBuffer != nullptr);

        if (fBuffer != nullptr)
            delete[] fBuffer;
    }
}

void CarlaEngineEventPort::initBuffer(CarlaEngine* const engine)
{
    CARLA_ASSERT(engine != nullptr);

    if (engine == nullptr)
        return;

    if (kProcessMode == PROCESS_MODE_CONTINUOUS_RACK)
        fBuffer = engine->getRackEventBuffer(kIsInput);
    else if (kProcessMode == PROCESS_MODE_PATCHBAY && ! kIsInput)
        carla_zeroMem(fBuffer, sizeof(EngineEvent)*PATCHBAY_EVENT_COUNT);
}

uint32_t CarlaEngineEventPort::getEventCount()
{
    CARLA_ASSERT(kIsInput);
    CARLA_ASSERT(fBuffer != nullptr);

    if (! kIsInput)
        return 0;
    if (fBuffer == nullptr)
        return 0;

    if (kProcessMode == PROCESS_MODE_CONTINUOUS_RACK || kProcessMode == PROCESS_MODE_PATCHBAY)
    {
        uint32_t count = 0;
        const EngineEvent* const events = fBuffer;

        for (uint32_t i=0; i < kMaxEventCount; i++, count++)
        {
            if (events[i].type == kEngineEventTypeNull)
                break;
        }

        return count;
    }

    return 0;
}

const EngineEvent& CarlaEngineEventPort::getEvent(const uint32_t index)
{
    CARLA_ASSERT(kIsInput);
    CARLA_ASSERT(fBuffer != nullptr);
    CARLA_ASSERT(index < kMaxEventCount);

    if (! kIsInput)
        return kFallbackEngineEvent;
    if (fBuffer == nullptr)
        return kFallbackEngineEvent;
    if (index >= kMaxEventCount)
        return kFallbackEngineEvent;

    if (kProcessMode == PROCESS_MODE_CONTINUOUS_RACK || kProcessMode == PROCESS_MODE_PATCHBAY)
    {
        const EngineEvent* const events = fBuffer;

        return events[index];
    }

    return kFallbackEngineEvent;
}

void CarlaEngineEventPort::writeControlEvent(const uint32_t time, const uint8_t channel, const EngineControlEventType type, const uint16_t param, const double value)
{
    CARLA_ASSERT(! kIsInput);
    CARLA_ASSERT(fBuffer != nullptr);
    CARLA_ASSERT(type != kEngineControlEventTypeNull);
    CARLA_ASSERT(channel < MAX_MIDI_CHANNELS);
    CARLA_ASSERT(value >= 0.0 && value <= 1.0);

    if (kIsInput)
        return;
    if (fBuffer == nullptr)
        return;
    if (type == kEngineControlEventTypeNull)
        return;
    if (channel >= MAX_MIDI_CHANNELS)
        return;
    if (type == kEngineControlEventTypeParameter)
    {
        CARLA_ASSERT(! MIDI_IS_CONTROL_BANK_SELECT(param));
    }

    if (kProcessMode == PROCESS_MODE_CONTINUOUS_RACK || kProcessMode == PROCESS_MODE_PATCHBAY)
    {
        EngineEvent* const events = fBuffer;

        for (uint32_t i=0; i < kMaxEventCount; i++)
        {
            if (events[i].type != kEngineEventTypeNull)
                continue;

            events[i].type    = kEngineEventTypeControl;
            events[i].time    = time;
            events[i].channel = channel;

            events[i].ctrl.type  = type;
            events[i].ctrl.param = param;
            events[i].ctrl.value = value;

            return;
        }

        qWarning("CarlaEngineEventPort::writeControlEvent() - buffer full");
    }
}

void CarlaEngineEventPort::writeMidiEvent(const uint32_t time, const uint8_t channel, const uint8_t port, const uint8_t* const data, const uint8_t size)
{
    CARLA_ASSERT(! kIsInput);
    CARLA_ASSERT(fBuffer != nullptr);
    CARLA_ASSERT(channel < MAX_MIDI_CHANNELS);
    CARLA_ASSERT(data != nullptr);
    CARLA_ASSERT(size > 0);

    if (kIsInput)
        return;
    if (fBuffer == nullptr)
        return;
    if (channel >= MAX_MIDI_CHANNELS)
        return;
    if (data == nullptr)
        return;
    if (size == 0)
        return;

    if (kProcessMode == PROCESS_MODE_CONTINUOUS_RACK || kProcessMode == PROCESS_MODE_PATCHBAY)
    {
        if (size > 3)
            return;

        EngineEvent* const events = fBuffer;

        for (uint32_t i=0; i < kMaxEventCount; i++)
        {
            if (events[i].type != kEngineEventTypeNull)
                continue;

            events[i].type    = kEngineEventTypeMidi;
            events[i].time    = time;
            events[i].channel = channel;

            events[i].midi.port    = port;
            events[i].midi.data[0] = data[0];
            events[i].midi.data[1] = data[1];
            events[i].midi.data[2] = data[2];
            events[i].midi.size    = size;

            return;
        }

        qWarning("CarlaEngineEventPort::writeMidiEvent() - buffer full");
    }
}

// -------------------------------------------------------------------------------------------------------------------
// Carla Engine client (Abstract)

CarlaEngineClient::CarlaEngineClient(const EngineType engineType, const ProcessMode processMode)
    : kEngineType(engineType),
      kProcessMode(processMode),
      fActive(false),
      fLatency(0)
{
    qDebug("CarlaEngineClient::CarlaEngineClient(%s, %s)", EngineType2Str(engineType), ProcessMode2Str(processMode));
    CARLA_ASSERT(engineType != kEngineTypeNull);
}

CarlaEngineClient::~CarlaEngineClient()
{
    qDebug("CarlaEngineClient::~CarlaEngineClient()");
    CARLA_ASSERT(! fActive);
}

void CarlaEngineClient::activate()
{
    qDebug("CarlaEngineClient::activate()");
    CARLA_ASSERT(! fActive);

    fActive = true;
}

void CarlaEngineClient::deactivate()
{
    qDebug("CarlaEngineClient::deactivate()");
    CARLA_ASSERT(fActive);

    fActive = false;
}

bool CarlaEngineClient::isActive() const
{
    qDebug("CarlaEngineClient::isActive()");

    return fActive;
}

bool CarlaEngineClient::isOk() const
{
    qDebug("CarlaEngineClient::isOk()");

    return true;
}

uint32_t CarlaEngineClient::getLatency() const
{
    return fLatency;
}

void CarlaEngineClient::setLatency(const uint32_t samples)
{
    fLatency = samples;
}

const CarlaEnginePort* CarlaEngineClient::addPort(const EnginePortType portType, const char* const name, const bool isInput)
{
    qDebug("CarlaEngineClient::addPort(%s, \"%s\", %s)", EnginePortType2Str(portType), name, bool2str(isInput));

    switch (portType)
    {
    case kEnginePortTypeNull:
        break;
    case kEnginePortTypeAudio:
        return new CarlaEngineAudioPort(isInput, kProcessMode);
    case kEnginePortTypeEvent:
        return new CarlaEngineEventPort(isInput, kProcessMode);
    }

    qCritical("CarlaEngineClient::addPort(%s, \"%s\", %s) - invalid type", EnginePortType2Str(portType), name, bool2str(isInput));
    return nullptr;
}

// -------------------------------------------------------------------------------------------------------------------
// Carla Engine

CarlaEngine::CarlaEngine()
    : fBufferSize(0),
      fSampleRate(0.0),
      kData(new CarlaEngineProtectedData(this))
{
    qDebug("CarlaEngine::CarlaEngine()");
}

CarlaEngine::~CarlaEngine()
{
    qDebug("CarlaEngine::~CarlaEngine()");

    delete kData;
}

// -----------------------------------------------------------------------
// Helpers

void doIdle(CarlaEngineProtectedData* const kData, const bool unlock)
{
    kData->nextAction.opcode = EnginePostActionNull;

    if (unlock)
        kData->nextAction.mutex.unlock();
}

void doPluginRemove(CarlaEngineProtectedData* const kData, const bool unlock)
{
    CARLA_ASSERT(kData->curPluginCount > 0);
    kData->curPluginCount--;

    const unsigned int id = kData->nextAction.pluginId;

    // reset current plugin
    kData->plugins[id].plugin = nullptr;

    CarlaPlugin* plugin;

    // move all plugins 1 spot backwards
    for (unsigned int i=id; i < kData->curPluginCount; i++)
    {
        plugin = kData->plugins[i+1].plugin;

        CARLA_ASSERT(plugin);

        if (plugin == nullptr)
            break;

        plugin->setId(i);

        kData->plugins[i].plugin      = plugin;
        kData->plugins[i].insPeak[0]  = 0.0f;
        kData->plugins[i].insPeak[1]  = 0.0f;
        kData->plugins[i].outsPeak[0] = 0.0f;
        kData->plugins[i].outsPeak[1] = 0.0f;
    }

    kData->nextAction.opcode = EnginePostActionNull;

    if (unlock)
        kData->nextAction.mutex.unlock();
}

// -----------------------------------------------------------------------
// Static values and calls

unsigned int CarlaEngine::getDriverCount()
{
    qDebug("CarlaEngine::getDriverCount()");

    unsigned int count = 0;

#ifdef WANT_JACK
    count += 1;
#endif
#ifdef WANT_RTAUDIO
    count += getRtAudioApiCount();
#endif

    return count;
}

const char* CarlaEngine::getDriverName(unsigned int index)
{
    qDebug("CarlaEngine::getDriverName(%i)", index);

#ifdef WANT_JACK
    if (index == 0)
        return "JACK";
    else
        index -= 1;
#endif

#ifdef WANT_RTAUDIO
    if (index < getRtAudioApiCount())
        return getRtAudioApiName(index);
#endif

    qWarning("CarlaEngine::getDriverName(%i) - invalid index", index);
    return nullptr;
}

CarlaEngine* CarlaEngine::newDriverByName(const char* const driverName)
{
    qDebug("CarlaEngine::newDriverByName(\"%s\")", driverName);

#ifdef WANT_JACK
    if (strcmp(driverName, "JACK") == 0)
        return newJack();
#else
    if (false)
        pass();
#endif

#ifdef WANT_RTAUDIO
# ifdef __LINUX_ALSA__
    else if (strcmp(driverName, "ALSA") == 0)
        return newRtAudio(RTAUDIO_LINUX_ALSA);
# endif
# ifdef __LINUX_PULSE__
    else if (strcmp(driverName, "PulseAudio") == 0)
        return newRtAudio(RTAUDIO_LINUX_PULSE);
# endif
# ifdef __LINUX_OSS__
    else if (strcmp(driverName, "OSS") == 0)
        return newRtAudio(RTAUDIO_LINUX_OSS);
# endif
# ifdef __UNIX_JACK__
    else if (strcmp(driverName, "JACK (RtAudio)") == 0)
        return newRtAudio(RTAUDIO_UNIX_JACK);
# endif
# ifdef __MACOSX_CORE__
    else if (strcmp(driverName, "CoreAudio") == 0)
        return newRtAudio(RTAUDIO_MACOSX_CORE);
# endif
# ifdef __WINDOWS_ASIO__
    else if (strcmp(driverName, "ASIO") == 0)
        return newRtAudio(RTAUDIO_WINDOWS_ASIO);
# endif
# ifdef __WINDOWS_DS__
    else if (strcmp(driverName, "DirectSound") == 0)
        return newRtAudio(RTAUDIO_WINDOWS_DS);
# endif
#endif

    return nullptr;
}

// -----------------------------------------------------------------------
// Maximum values

unsigned int CarlaEngine::maxClientNameSize() const
{
    return STR_MAX/2;
}

unsigned int CarlaEngine::maxPortNameSize() const
{
    return STR_MAX;
}

unsigned int CarlaEngine::currentPluginCount() const
{
    return kData->curPluginCount;
}

unsigned int CarlaEngine::maxPluginNumber() const
{
    return kData->maxPluginNumber;
}

// -----------------------------------------------------------------------
// Virtual, per-engine type calls

bool CarlaEngine::init(const char* const clientName)
{
    qDebug("CarlaEngine::init(\"%s\")", clientName);
    CARLA_ASSERT(kData->plugins == nullptr);

    fName = clientName;
    fName.toBasic();

    fTimeInfo.clear();

    kData->aboutToClose = false;
    kData->curPluginCount = 0;

    switch (fOptions.processMode)
    {
    case PROCESS_MODE_CONTINUOUS_RACK:
        kData->maxPluginNumber = MAX_RACK_PLUGINS;
        break;
    case PROCESS_MODE_PATCHBAY:
        kData->maxPluginNumber = MAX_PATCHBAY_PLUGINS;
        break;
    case PROCESS_MODE_BRIDGE:
        kData->maxPluginNumber = 1;
        break;
    default:
        kData->maxPluginNumber = MAX_DEFAULT_PLUGINS;
        break;
    }

    kData->plugins = new EnginePluginData[kData->maxPluginNumber];

    kData->osc.init(clientName);

#ifndef BUILD_BRIDGE
    kData->oscData = kData->osc.getControlData();
#else
    kData->oscData = nullptr; // set later in setOscBridgeData()
#endif

#ifndef BUILD_BRIDGE
    //if (strcmp(clientName, "Carla") != 0)
    carla_setprocname(clientName);
#endif

    kData->nextAction.ready();
    kData->thread.startNow();

    return true;
}

bool CarlaEngine::close()
{
    qDebug("CarlaEngine::close()");
    CARLA_ASSERT(kData->plugins != nullptr);

    kData->nextAction.ready();
    kData->thread.stopNow();

#ifndef BUILD_BRIDGE
    osc_send_control_exit();
#endif
    kData->osc.close();

    kData->oscData = nullptr;

    kData->aboutToClose = true;
    kData->curPluginCount = 0;
    kData->maxPluginNumber = 0;

    if (kData->plugins != nullptr)
    {
        delete[] kData->plugins;
        kData->plugins = nullptr;
    }

    fName.clear();

    return true;
}

void CarlaEngine::idle()
{
    CARLA_ASSERT(kData->plugins != nullptr);
    CARLA_ASSERT(isRunning());

    for (unsigned int i=0; i < kData->curPluginCount; i++)
    {
        CarlaPlugin* const plugin = kData->plugins[i].plugin;

        if (plugin && plugin->enabled())
            plugin->idleGui();
    }
}

// -----------------------------------------------------------------------
// Virtual, per-engine type calls

CarlaEngineClient* CarlaEngine::addClient(CarlaPlugin* const)
{
    return new CarlaEngineClient(type(), fOptions.processMode);
}

// -----------------------------------------------------------------------
// Plugin management

bool CarlaEngine::addPlugin(const BinaryType btype, const PluginType ptype, const char* const filename, const char* const name, const char* const label, const void* const extra)
{
    qDebug("CarlaEngine::addPlugin(%s, %s, \"%s\", \"%s\", \"%s\", %p)", BinaryType2Str(btype), PluginType2Str(ptype), filename, name, label, extra);
    CARLA_ASSERT(btype != BINARY_NONE);
    CARLA_ASSERT(ptype != PLUGIN_NONE);
    CARLA_ASSERT(filename);
    CARLA_ASSERT(label);

    if (kData->curPluginCount == kData->maxPluginNumber)
    {
        setLastError("Maximum number of plugins reached");
        return false;
    }

    const unsigned int id = kData->curPluginCount;

    CarlaPlugin::Initializer init = {
        this,
        id,
        filename,
        name,
        label
    };

    CarlaPlugin* plugin = nullptr;

#ifndef BUILD_BRIDGE
    const char* bridgeBinary;

    switch (btype)
    {
    case BINARY_POSIX32:
        bridgeBinary = fOptions.bridge_posix32.isNotEmpty() ? (const char*)fOptions.bridge_posix32 : nullptr;
        break;
    case BINARY_POSIX64:
        bridgeBinary = fOptions.bridge_posix64.isNotEmpty() ? (const char*)fOptions.bridge_posix64 : nullptr;
        break;
    case BINARY_WIN32:
        bridgeBinary = fOptions.bridge_win32.isNotEmpty() ? (const char*)fOptions.bridge_win32 : nullptr;
        break;
    case BINARY_WIN64:
        bridgeBinary = fOptions.bridge_win64.isNotEmpty() ? (const char*)fOptions.bridge_win64 : nullptr;
        break;
    default:
        bridgeBinary = nullptr;
        break;
    }

#ifndef Q_OS_WIN
    if (btype == BINARY_NATIVE && fOptions.bridge_native.isNotEmpty())
        bridgeBinary = (const char*)fOptions.bridge_native;
#endif

    if (fOptions.preferPluginBridges && bridgeBinary != nullptr)
    {
        // TODO
        if (fOptions.processMode != PROCESS_MODE_MULTIPLE_CLIENTS)
        {
            setLastError("Can only use bridged plugins in JACK Multi-Client mode");
            return -1;
        }

        // TODO
        if (type() != kEngineTypeJack)
        {
            setLastError("Can only use bridged plugins with JACK backend");
            return -1;
        }

#if 0
        plugin = CarlaPlugin::newBridge(init, btype, ptype, bridgeBinary);
#endif
        setLastError("Bridged plugins are not implemented yet");
    }
    else
#endif // BUILD_BRIDGE
    {
        switch (ptype)
        {
        case PLUGIN_NONE:
            break;

#ifndef BUILD_BRIDGE
        case PLUGIN_INTERNAL:
            plugin = CarlaPlugin::newNative(init);
            break;
#endif

        case PLUGIN_LADSPA:
            plugin = CarlaPlugin::newLADSPA(init, (const LADSPA_RDF_Descriptor*)extra);
            break;

        case PLUGIN_DSSI:
            plugin = CarlaPlugin::newDSSI(init, (const char*)extra);
            break;

        case PLUGIN_LV2:
            plugin = CarlaPlugin::newLV2(init);
            break;

        case PLUGIN_VST:
            plugin = CarlaPlugin::newVST(init);
            break;

        case PLUGIN_GIG:
            plugin = CarlaPlugin::newGIG(init);
            break;

        case PLUGIN_SF2:
            plugin = CarlaPlugin::newSF2(init);
            break;

        case PLUGIN_SFZ:
            plugin = CarlaPlugin::newSFZ(init);
            break;
        }
    }

    if (plugin == nullptr)
        return false;

    kData->plugins[id].plugin      = plugin;
    kData->plugins[id].insPeak[0]  = 0.0f;
    kData->plugins[id].insPeak[1]  = 0.0f;
    kData->plugins[id].outsPeak[0] = 0.0f;
    kData->plugins[id].outsPeak[1] = 0.0f;

    kData->curPluginCount += 1;

    // FIXME
    callback(CALLBACK_PLUGIN_ADDED, id, 0, 0, 0.0f, nullptr);

    return true;
}

bool CarlaEngine::removePlugin(const unsigned int id)
{
    qDebug("CarlaEngine::removePlugin(%i)", id);
    CARLA_ASSERT(kData->curPluginCount > 0);
    CARLA_ASSERT(id < kData->curPluginCount);
    CARLA_ASSERT(kData->plugins != nullptr);

    if (kData->plugins == nullptr)
    {
        setLastError("Critical error: no plugins are currently loaded!");
        return false;
    }

    CarlaPlugin* const plugin = kData->plugins[id].plugin;

    CARLA_ASSERT(plugin);

    if (plugin)
    {
        CARLA_ASSERT(plugin->id() == id);

        kData->thread.stopNow();

        kData->nextAction.pluginId = id;
        kData->nextAction.opcode   = EnginePostActionRemovePlugin;

        kData->nextAction.mutex.lock();

        if (isRunning())
        {
            // block wait for unlock on proccessing side
            kData->nextAction.mutex.lock();
        }
        else
        {
            doPluginRemove(kData, false);
        }

#ifndef BUILD_BRIDGE
        if (isOscControlRegistered())
            osc_send_control_remove_plugin(id);
#endif

        delete plugin;

        kData->nextAction.mutex.unlock();

        if (isRunning() && ! kData->aboutToClose)
            kData->thread.startNow();

        // FIXME
        callback(CALLBACK_PLUGIN_REMOVED, id, 0, 0, 0.0f, nullptr);

        return true;
    }

    qCritical("CarlaEngine::removePlugin(%i) - could not find plugin", id);
    setLastError("Could not find plugin to remove");
    return false;
}

void CarlaEngine::removeAllPlugins()
{
    qDebug("CarlaEngine::removeAllPlugins()");

    kData->thread.stopNow();

    const unsigned int oldCount = kData->curPluginCount;

    kData->curPluginCount = 0;

    // wait for processing
    waitForProccessEnd();

    for (unsigned int i=0; i < oldCount; i++)
    {
        CarlaPlugin* const plugin = kData->plugins[i].plugin;

        CARLA_ASSERT(plugin);

        if (plugin)
            delete plugin;

        // clear this plugin
        kData->plugins[i].plugin      = nullptr;
        kData->plugins[i].insPeak[0]  = 0.0;
        kData->plugins[i].insPeak[1]  = 0.0;
        kData->plugins[i].outsPeak[0] = 0.0;
        kData->plugins[i].outsPeak[1] = 0.0;
    }

    if (isRunning() && ! kData->aboutToClose)
        kData->thread.startNow();
}

CarlaPlugin* CarlaEngine::getPlugin(const unsigned int id) const
{
    qDebug("CarlaEngine::getPlugin(%i) [count:%i]", id, kData->curPluginCount);
    CARLA_ASSERT(kData->curPluginCount > 0);
    CARLA_ASSERT(id < kData->curPluginCount);
    CARLA_ASSERT(kData->plugins != nullptr);

    if (id < kData->curPluginCount && kData->plugins != nullptr)
        return kData->plugins[id].plugin;

    return nullptr;
}

CarlaPlugin* CarlaEngine::getPluginUnchecked(const unsigned int id) const
{
    return kData->plugins[id].plugin;
}

const char* CarlaEngine::getNewUniquePluginName(const char* const name)
{
    qDebug("CarlaEngine::getNewUniquePluginName(\"%s\")", name);
    CARLA_ASSERT(kData->maxPluginNumber > 0);
    CARLA_ASSERT(kData->plugins != nullptr);
    CARLA_ASSERT(name != nullptr);

    static CarlaString sname;
    sname = name;

    if (sname.isEmpty() || kData->plugins == nullptr)
    {
        sname = "(No name)";
        return (const char*)sname;
    }

    sname.truncate(maxClientNameSize()-5-1); // 5 = strlen(" (10)")
    sname.replace(':', '.'); // ':' is used in JACK1 to split client/port names

    for (unsigned short i=0; i < kData->curPluginCount; i++)
    {
        CARLA_ASSERT(kData->plugins[i].plugin);

        // Check if unique name doesn't exist
        if (const char* const pluginName = kData->plugins[i].plugin->name())
        {
            if (sname != pluginName)
                continue;
        }

        // Check if string has already been modified
        {
            const size_t len = sname.length();

            // 1 digit, ex: " (2)"
            if (sname[len-4] == ' ' && sname[len-3] == '(' && sname.isDigit(len-2) && sname[len-1] == ')')
            {
                int number = sname[len-2] - '0';

                if (number == 9)
                {
                    // next number is 10, 2 digits
                    sname.truncate(len-4);
                    sname += " (10)";
                    //sname.replace(" (9)", " (10)");
                }
                else
                    sname[len-2] = char('0' + number + 1);

                continue;
            }

            // 2 digits, ex: " (11)"
            if (sname[len-5] == ' ' && sname[len-4] == '(' && sname.isDigit(len-3) && sname.isDigit(len-2) && sname[len-1] == ')')
            {
                char n2 = sname[len-2];
                char n3 = sname[len-3];

                if (n2 == '9')
                {
                    n2 = '0';
                    n3 = char(n3 + 1);
                }
                else
                    n2 = char(n2 + 1);

                sname[len-2] = n2;
                sname[len-3] = n3;

                continue;
            }
        }

        // Modify string if not
        sname += " (2)";
    }

    return (const char*)sname;
}

#if 0
void CarlaEngine::__bridgePluginRegister(const unsigned short id, CarlaPlugin* const plugin)
{
    data->carlaPlugins[id] = plugin;
}
#endif

// -----------------------------------------------------------------------
// Information (base)

bool CarlaEngine::loadProject(const char* const filename)
{
    CARLA_ASSERT(filename != nullptr);

    //QFile file(filename);

    //if (! file.open(QIODevice::WriteOnly | QIODevice::Text))
    //    return;

    //getSaveStateDictFromXML
    return true;
}

bool CarlaEngine::saveProject(const char* const filename)
{
    CARLA_ASSERT(filename != nullptr);

    QFile file(filename);
    file.open(QIODevice::WriteOnly | QIODevice::Text);

    QTextStream out(&file);
    out << "<?xml version='1.0' encoding='UTF-8'?>\n";
    out << "<!DOCTYPE CARLA-PRESET>\n";
    out << "<CARLA-PRESET VERSION='0.5.0'>\n";

    for (unsigned int i=0; i < kData->curPluginCount; i++)
    {
        CarlaPlugin* const plugin = kData->plugins[i].plugin;

        if (plugin != nullptr && plugin->enabled())
        {
            const SaveState& saveState = plugin->getSaveState();
            // TODO
        }
    }

    out << "</CARLA-PRESET>\n";

    file.close();

    return true;
}

// -----------------------------------------------------------------------
// Information (peaks)

float CarlaEngine::getInputPeak(const unsigned int pluginId, const unsigned short id) const
{
    CARLA_ASSERT(pluginId < kData->curPluginCount);
    CARLA_ASSERT(id < MAX_PEAKS);

    return kData->plugins[pluginId].insPeak[id];
}

float CarlaEngine::getOutputPeak(const unsigned int pluginId, const unsigned short id) const
{
    CARLA_ASSERT(pluginId < kData->curPluginCount);
    CARLA_ASSERT(id < MAX_PEAKS);

    return kData->plugins[pluginId].outsPeak[id];
}

// -----------------------------------------------------------------------
// Callback

void CarlaEngine::callback(const CallbackType action, const unsigned int pluginId, const int value1, const int value2, const float value3, const char* const valueStr)
{
    qDebug("CarlaEngine::callback(%s, %i, %i, %i, %f, \"%s\")", CallbackType2Str(action), pluginId, value1, value2, value3, valueStr);

    if (kData->callback)
        kData->callback(kData->callbackPtr, action, pluginId, value1, value2, value3, valueStr);
}

void CarlaEngine::setCallback(const CallbackFunc func, void* const ptr)
{
    qDebug("CarlaEngine::setCallback(%p, %p)", func, ptr);
    CARLA_ASSERT(func);

    kData->callback    = func;
    kData->callbackPtr = ptr;
}

// -----------------------------------------------------------------------
// Error handling

const char* CarlaEngine::getLastError() const
{
    return (const char*)kData->lastError;
}

void CarlaEngine::setLastError(const char* const error)
{
    kData->lastError = error;
}

void CarlaEngine::setAboutToClose()
{
    qDebug("CarlaEngine::setAboutToClose()");
    kData->aboutToClose = true;
}

// -----------------------------------------------------------------------
// Misc

void CarlaEngine::waitForProccessEnd()
{
    qDebug("CarlaEngine::waitForProccessEnd()");

    kData->nextAction.pluginId = 0;
    kData->nextAction.opcode   = EnginePostActionIdle;

    kData->nextAction.mutex.lock();

    if (isRunning())
    {
        // block wait for unlock on proccessing side
        kData->nextAction.mutex.lock();
    }
    else
    {
        doIdle(kData, false);
    }

    kData->nextAction.mutex.unlock();
}

// -----------------------------------------------------------------------
// Global options

#ifndef BUILD_BRIDGE

const QProcessEnvironment& CarlaEngine::getOptionsAsProcessEnvironment() const
{
    return kData->procEnv;
}

#define CARLA_ENGINE_SET_OPTION_RUNNING_CHECK \
    if (isRunning()) \
        return qCritical("CarlaEngine::setOption(%s, %i, \"%s\") - Cannot set this option while engine is running!", OptionsType2Str(option), value, valueStr);

void CarlaEngine::setOption(const OptionsType option, const int value, const char* const valueStr)
{
    qDebug("CarlaEngine::setOption(%s, %i, \"%s\")", OptionsType2Str(option), value, valueStr);

    switch (option)
    {
    case OPTION_PROCESS_NAME:
        carla_setprocname(valueStr);
        break;

    case OPTION_PROCESS_MODE:
        CARLA_ENGINE_SET_OPTION_RUNNING_CHECK

        if (value < PROCESS_MODE_SINGLE_CLIENT || value > PROCESS_MODE_PATCHBAY)
            return qCritical("CarlaEngine::setOption(%s, %i, \"%s\") - invalid value", OptionsType2Str(option), value, valueStr);

        fOptions.processMode = static_cast<ProcessMode>(value);
        break;

    case OPTION_MAX_PARAMETERS:
        CARLA_ENGINE_SET_OPTION_RUNNING_CHECK

        if (value < 0)
            return; // TODO error here

        fOptions.maxParameters = static_cast<uint>(value);
        break;

    case OPTION_PREFERRED_BUFFER_SIZE:
        CARLA_ENGINE_SET_OPTION_RUNNING_CHECK
        fOptions.preferredBufferSize = static_cast<uint>(value);
        break;

    case OPTION_PREFERRED_SAMPLE_RATE:
        CARLA_ENGINE_SET_OPTION_RUNNING_CHECK
        fOptions.preferredSampleRate = static_cast<uint>(value);
        break;

    case OPTION_FORCE_STEREO:
        CARLA_ENGINE_SET_OPTION_RUNNING_CHECK
        fOptions.forceStereo = (value != 0);
        break;

#ifdef WANT_DSSI
    case OPTION_USE_DSSI_VST_CHUNKS:
        CARLA_ENGINE_SET_OPTION_RUNNING_CHECK
        fOptions.useDssiVstChunks = (value != 0);
        break;
#endif

    case OPTION_PREFER_PLUGIN_BRIDGES:
        CARLA_ENGINE_SET_OPTION_RUNNING_CHECK
        fOptions.preferPluginBridges = (value != 0);
        break;

    case OPTION_PREFER_UI_BRIDGES:
        CARLA_ENGINE_SET_OPTION_RUNNING_CHECK
        fOptions.preferUiBridges = (value != 0);
        break;

    case OPTION_OSC_UI_TIMEOUT:
        CARLA_ENGINE_SET_OPTION_RUNNING_CHECK
        fOptions.oscUiTimeout = static_cast<uint>(value);
        break;

    case OPTION_PATH_BRIDGE_NATIVE:
        fOptions.bridge_native = valueStr;
        break;
    case OPTION_PATH_BRIDGE_POSIX32:
        fOptions.bridge_posix32 = valueStr;
        break;
    case OPTION_PATH_BRIDGE_POSIX64:
        fOptions.bridge_posix64 = valueStr;
        break;
    case OPTION_PATH_BRIDGE_WIN32:
        fOptions.bridge_win32 = valueStr;
        break;
    case OPTION_PATH_BRIDGE_WIN64:
        fOptions.bridge_win64 = valueStr;
        break;

#ifdef WANT_LV2
    case OPTION_PATH_BRIDGE_LV2_GTK2:
        fOptions.bridge_lv2gtk2 = valueStr;
        break;
    case OPTION_PATH_BRIDGE_LV2_GTK3:
        fOptions.bridge_lv2gtk3 = valueStr;
        break;
    case OPTION_PATH_BRIDGE_LV2_QT4:
        fOptions.bridge_lv2qt4 = valueStr;
        break;
    case OPTION_PATH_BRIDGE_LV2_QT5:
        fOptions.bridge_lv2qt5 = valueStr;
        break;
    case OPTION_PATH_BRIDGE_LV2_COCOA:
        fOptions.bridge_lv2cocoa = valueStr;
        break;
    case OPTION_PATH_BRIDGE_LV2_WINDOWS:
        fOptions.bridge_lv2win = valueStr;
        break;
    case OPTION_PATH_BRIDGE_LV2_X11:
        fOptions.bridge_lv2x11 = valueStr;
        break;
#endif

#ifdef WANT_VST
    case OPTION_PATH_BRIDGE_VST_COCOA:
        fOptions.bridge_vstcocoa = valueStr;
        break;
    case OPTION_PATH_BRIDGE_VST_HWND:
        fOptions.bridge_vsthwnd = valueStr;
        break;
    case OPTION_PATH_BRIDGE_VST_X11:
        fOptions.bridge_vstx11 = valueStr;
        break;
#endif
    }
}
#endif

// -----------------------------------------------------------------------
// OSC Stuff

#ifdef BUILD_BRIDGE
bool CarlaEngine::isOscBridgeRegistered() const
{
    return (kData->oscData != nullptr);
}
#else
bool CarlaEngine::isOscControlRegistered() const
{
    return kData->osc.isControlRegistered();
}
#endif

void CarlaEngine::idleOsc()
{
    kData->osc.idle();
}

const char* CarlaEngine::getOscServerPathTCP() const
{
    return kData->osc.getServerPathTCP();
}

const char* CarlaEngine::getOscServerPathUDP() const
{
    return kData->osc.getServerPathUDP();
}

#ifdef BUILD_BRIDGE
void CarlaEngine::setOscBridgeData(const CarlaOscData* const oscData)
{
    kData->oscData = oscData;
}
#endif

// -----------------------------------------------------------------------
// protected calls

void CarlaEngine::bufferSizeChanged(const uint32_t newBufferSize)
{
    qDebug("CarlaEngine::bufferSizeChanged(%i)", newBufferSize);

    for (unsigned int i=0; i < kData->curPluginCount; i++)
    {
        CarlaPlugin* const plugin = kData->plugins[i].plugin;

        if (plugin != nullptr && plugin->enabled())
            plugin->bufferSizeChanged(newBufferSize);
    }
}

void CarlaEngine::sampleRateChanged(const double newSampleRate)
{
    qDebug("CarlaEngine::sampleRateChanged(%g)", newSampleRate);

    for (unsigned int i=0; i < kData->curPluginCount; i++)
    {
        CarlaPlugin* const plugin = kData->plugins[i].plugin;

        if (plugin != nullptr && plugin->enabled())
            plugin->sampleRateChanged(newSampleRate);
    }
}

void CarlaEngine::proccessPendingEvents()
{
    switch (kData->nextAction.opcode)
    {
    case EnginePostActionNull:
        break;
    case EnginePostActionIdle:
        doIdle(kData, true);
        break;
    case EnginePostActionRemovePlugin:
        doPluginRemove(kData, true);
        break;
    }

    for (unsigned int i=0; i < kData->curPluginCount; i++)
    {
        // TODO - peak values
    }
}

void CarlaEngine::setPeaks(const unsigned int pluginId, float* inPeaks, float* outPeaks)
{
    kData->plugins[pluginId].insPeak[0]  = inPeaks[0];
    kData->plugins[pluginId].insPeak[1]  = inPeaks[1];
    kData->plugins[pluginId].outsPeak[0] = outPeaks[0];
    kData->plugins[pluginId].outsPeak[1] = outPeaks[1];
}

#ifndef BUILD_BRIDGE
EngineEvent* CarlaEngine::getRackEventBuffer(const bool isInput)
{
    // TODO
    return nullptr;
    Q_UNUSED(isInput);
}

void CarlaEngine::processRack(float* inBuf[2], float* outBuf[2], const uint32_t frames)
{
    // initialize outputs (zero)
    carla_zeroFloat(outBuf[0], frames);
    carla_zeroFloat(outBuf[1], frames);

    //std::memset(rackEventsOut, 0, sizeof(EngineEvent)*MAX_EVENTS);

    bool processed = false;
    // process plugins
    for (unsigned int i=0; i < kData->curPluginCount; i++)
    {
        CarlaPlugin* const plugin = getPluginUnchecked(i);

        if (plugin == nullptr || ! plugin->enabled())
            continue;

#if 0
        if (processed)
        {
            // initialize inputs (from previous outputs)
            memcpy(inBuf[0], outBuf[0], sizeof(float)*frames);
            memcpy(inBuf[1], outBuf[1], sizeof(float)*frames);
            memcpy(rackMidiEventsIn, rackMidiEventsOut, sizeof(CarlaEngineMidiEvent)*MAX_MIDI_EVENTS);

            // initialize outputs (zero)
            carla_zeroFloat(outBuf[0], frames);
            carla_zeroFloat(outBuf[1], frames);
            memset(rackMidiEventsOut, 0, sizeof(CarlaEngineMidiEvent)*MAX_MIDI_EVENTS);
        }

        // process
        processLock();
        plugin->initBuffers();

        if (false /*plugin->data->processHighPrecision*/)
        {
            float* inBuf2[2];
            float* outBuf2[2];

            for (uint32_t j=0; j < frames; j += 8)
            {
                inBuf2[0] = inBuf[0] + j;
                inBuf2[1] = inBuf[1] + j;

                outBuf2[0] = outBuf[0] + j;
                outBuf2[1] = outBuf[1] + j;

                plugin->process(inBuf2, outBuf2, 8, j);
            }
        }
        else
            plugin->process(inBuf, outBuf, frames);

        processUnlock();

        // if plugin has no audio inputs, add previous buffers
        if (plugin->audioInCount() == 0)
        {
            for (uint32_t j=0; j < frames; j++)
            {
                outBuf[0][j] += inBuf[0][j];
                outBuf[1][j] += inBuf[1][j];
            }
        }

        // if plugin has no midi output, add previous midi input
        if (plugin->midiOutCount() == 0)
        {
            memcpy(rackMidiEventsOut, rackMidiEventsIn, sizeof(CarlaEngineMidiEvent)*MAX_MIDI_EVENTS);
        }

        // set peaks
        {
            double inPeak1  = 0.0;
            double inPeak2  = 0.0;
            double outPeak1 = 0.0;
            double outPeak2 = 0.0;

            for (uint32_t k=0; k < frames; k++)
            {
                // TODO - optimize this
                if (std::abs(inBuf[0][k]) > inPeak1)
                    inPeak1 = std::abs(inBuf[0][k]);
                if (std::abs(inBuf[1][k]) > inPeak2)
                    inPeak2 = std::abs(inBuf[1][k]);
                if (std::abs(outBuf[0][k]) > outPeak1)
                    outPeak1 = std::abs(outBuf[0][k]);
                if (std::abs(outBuf[1][k]) > outPeak2)
                    outPeak2 = std::abs(outBuf[1][k]);
            }

            data->insPeak[i*MAX_PEAKS + 0] = inPeak1;
            data->insPeak[i*MAX_PEAKS + 1] = inPeak2;
            data->outsPeak[i*MAX_PEAKS + 0] = outPeak1;
            data->outsPeak[i*MAX_PEAKS + 1] = outPeak2;
        }
#endif

        processed = true;
    }

    // if no plugins in the rack, copy inputs over outputs
    if (! processed)
    {
        std::memcpy(outBuf[0], inBuf[0], sizeof(float)*frames);
        std::memcpy(outBuf[1], inBuf[1], sizeof(float)*frames);
        //std::memcpy(rackEventsOut, rackEventsIn, sizeof(EngineEvent)*MAX_EVENTS);
    }
}

void CarlaEngine::processPatchbay(float** inBuf, float** outBuf, const uint32_t bufCount[2], const uint32_t frames)
{
    // TODO
    Q_UNUSED(inBuf);
    Q_UNUSED(outBuf);
    Q_UNUSED(bufCount);
    Q_UNUSED(frames);
}
#endif

// -------------------------------------------------------------------------------------------------------------------
// Carla Engine OSC stuff

#ifdef BUILD_BRIDGE
void CarlaEngine::osc_send_peaks(CarlaPlugin* const /*plugin*/)
#else
void CarlaEngine::osc_send_peaks(CarlaPlugin* const plugin, const unsigned short id)
#endif
{
    // Peak values
    if (plugin->audioInCount() > 0)
    {
#ifdef BUILD_BRIDGE
        osc_send_bridge_set_inpeak(1);
        osc_send_bridge_set_inpeak(2);
#else
        osc_send_control_set_input_peak_value(id, 1);
        osc_send_control_set_input_peak_value(id, 2);
#endif
    }
    if (plugin->audioOutCount() > 0)
    {
#ifdef BUILD_BRIDGE
        osc_send_bridge_set_outpeak(1);
        osc_send_bridge_set_outpeak(2);
#else
        osc_send_control_set_output_peak_value(id, 1);
        osc_send_control_set_output_peak_value(id, 2);
#endif
    }
}

#ifndef BUILD_BRIDGE
void CarlaEngine::osc_send_control_add_plugin_start(const int32_t pluginId, const char* const pluginName)
{
    qDebug("CarlaEngine::osc_send_control_add_plugin_start(%i, \"%s\")", pluginId, pluginName);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->curPluginCount);
    CARLA_ASSERT(pluginName);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+18];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/add_plugin_start");
        lo_send(kData->oscData->target, target_path, "is", pluginId, pluginName);
    }
}

void CarlaEngine::osc_send_control_add_plugin_end(const int32_t pluginId)
{
    qDebug("CarlaEngine::osc_send_control_add_plugin_end(%i)", pluginId);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->curPluginCount);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+16];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/add_plugin_end");
        lo_send(kData->oscData->target, target_path, "i", pluginId);
    }
}

void CarlaEngine::osc_send_control_remove_plugin(const int32_t pluginId)
{
    qDebug("CarlaEngine::osc_send_control_remove_plugin(%i)", pluginId);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->curPluginCount);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+15];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/remove_plugin");
        lo_send(kData->oscData->target, target_path, "i", pluginId);
    }
}

void CarlaEngine::osc_send_control_set_plugin_data(const int32_t pluginId, const int32_t type, const int32_t category, const int32_t hints, const char* const realName, const char* const label, const char* const maker, const char* const copyright, const int64_t uniqueId)
{
    qDebug("CarlaEngine::osc_send_control_set_plugin_data(%i, %i, %i, %i, \"%s\", \"%s\", \"%s\", \"%s\", " P_INT64 ")", pluginId, type, category, hints, realName, label, maker, copyright, uniqueId);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->curPluginCount);
    CARLA_ASSERT(type != PLUGIN_NONE);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+17];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_plugin_data");
        lo_send(kData->oscData->target, target_path, "iiiissssh", pluginId, type, category, hints, realName, label, maker, copyright, uniqueId);
    }
}

void CarlaEngine::osc_send_control_set_plugin_ports(const int32_t pluginId, const int32_t audioIns, const int32_t audioOuts, const int32_t midiIns, const int32_t midiOuts, const int32_t cIns, const int32_t cOuts, const int32_t cTotals)
{
    qDebug("CarlaEngine::osc_send_control_set_plugin_ports(%i, %i, %i, %i, %i, %i, %i, %i)", pluginId, audioIns, audioOuts, midiIns, midiOuts, cIns, cOuts, cTotals);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->curPluginCount);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+18];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_plugin_ports");
        lo_send(kData->oscData->target, target_path, "iiiiiiii", pluginId, audioIns, audioOuts, midiIns, midiOuts, cIns, cOuts, cTotals);
    }
}

void CarlaEngine::osc_send_control_set_parameter_data(const int32_t pluginId, const int32_t index, const int32_t type, const int32_t hints, const char* const name, const char* const label, const double current)
{
    qDebug("CarlaEngine::osc_send_control_set_parameter_data(%i, %i, %i, %i, \"%s\", \"%s\", %g)", pluginId, index, type, hints, name, label, current);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->curPluginCount);
    CARLA_ASSERT(index >= 0);
    CARLA_ASSERT(type != PARAMETER_UNKNOWN);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+20];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_parameter_data");
        lo_send(kData->oscData->target, target_path, "iiiissd", pluginId, index, type, hints, name, label, current);
    }
}

void CarlaEngine::osc_send_control_set_parameter_ranges(const int32_t pluginId, const int32_t index, const double min, const double max, const double def, const double step, const double stepSmall, const double stepLarge)
{
    qDebug("CarlaEngine::osc_send_control_set_parameter_ranges(%i, %i, %g, %g, %g, %g, %g, %g)", pluginId, index, min, max, def, step, stepSmall, stepLarge);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->curPluginCount);
    CARLA_ASSERT(index >= 0);
    CARLA_ASSERT(min < max);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+22];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_parameter_ranges");
        lo_send(kData->oscData->target, target_path, "iidddddd", pluginId, index, min, max, def, step, stepSmall, stepLarge);
    }
}

void CarlaEngine::osc_send_control_set_parameter_midi_cc(const int32_t pluginId, const int32_t index, const int32_t cc)
{
    qDebug("CarlaEngine::osc_send_control_set_parameter_midi_cc(%i, %i, %i)", pluginId, index, cc);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->curPluginCount);
    CARLA_ASSERT(index >= 0);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+23];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_parameter_midi_cc");
        lo_send(kData->oscData->target, target_path, "iii", pluginId, index, cc);
    }
}

void CarlaEngine::osc_send_control_set_parameter_midi_channel(const int32_t pluginId, const int32_t index, const int32_t channel)
{
    qDebug("CarlaEngine::osc_send_control_set_parameter_midi_channel(%i, %i, %i)", pluginId, index, channel);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->curPluginCount);
    CARLA_ASSERT(index >= 0);
    CARLA_ASSERT(channel >= 0 && channel < 16);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+28];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_parameter_midi_channel");
        lo_send(kData->oscData->target, target_path, "iii", pluginId, index, channel);
    }
}

void CarlaEngine::osc_send_control_set_parameter_value(const int32_t pluginId, const int32_t index, const double value)
{
#if DEBUG
    if (index < 0)
        qDebug("CarlaEngine::osc_send_control_set_parameter_value(%i, %s, %g)", pluginId, InternalParametersIndex2Str((InternalParametersIndex)index), value);
    else
        qDebug("CarlaEngine::osc_send_control_set_parameter_value(%i, %i, %g)", pluginId, index, value);
#endif
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->curPluginCount);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+21];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_parameter_value");
        lo_send(kData->oscData->target, target_path, "iid", pluginId, index, value);
    }
}

void CarlaEngine::osc_send_control_set_default_value(const int32_t pluginId, const int32_t index, const double value)
{
    qDebug("CarlaEngine::osc_send_control_set_default_value(%i, %i, %g)", pluginId, index, value);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->maxPluginNumber);
    CARLA_ASSERT(index >= 0);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+19];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_default_value");
        lo_send(kData->oscData->target, target_path, "iid", pluginId, index, value);
    }
}

void CarlaEngine::osc_send_control_set_program(const int32_t pluginId, const int32_t index)
{
    qDebug("CarlaEngine::osc_send_control_set_program(%i, %i)", pluginId, index);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->maxPluginNumber);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+13];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_program");
        lo_send(kData->oscData->target, target_path, "ii", pluginId, index);
    }
}

void CarlaEngine::osc_send_control_set_program_count(const int32_t pluginId, const int32_t count)
{
    qDebug("CarlaEngine::osc_send_control_set_program_count(%i, %i)", pluginId, count);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->maxPluginNumber);
    CARLA_ASSERT(count >= 0);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+19];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_program_count");
        lo_send(kData->oscData->target, target_path, "ii", pluginId, count);
    }
}

void CarlaEngine::osc_send_control_set_program_name(const int32_t pluginId, const int32_t index, const char* const name)
{
    qDebug("CarlaEngine::osc_send_control_set_program_name(%i, %i, \"%s\")", pluginId, index, name);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->maxPluginNumber);
    CARLA_ASSERT(index >= 0);
    CARLA_ASSERT(name);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+18];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_program_name");
        lo_send(kData->oscData->target, target_path, "iis", pluginId, index, name);
    }
}

void CarlaEngine::osc_send_control_set_midi_program(const int32_t pluginId, const int32_t index)
{
    qDebug("CarlaEngine::osc_send_control_set_midi_program(%i, %i)", pluginId, index);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->maxPluginNumber);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+18];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_midi_program");
        lo_send(kData->oscData->target, target_path, "ii", pluginId, index);
    }
}

void CarlaEngine::osc_send_control_set_midi_program_count(const int32_t pluginId, const int32_t count)
{
    qDebug("CarlaEngine::osc_send_control_set_midi_program_count(%i, %i)", pluginId, count);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->maxPluginNumber);
    CARLA_ASSERT(count >= 0);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+24];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_midi_program_count");
        lo_send(kData->oscData->target, target_path, "ii", pluginId, count);
    }
}

void CarlaEngine::osc_send_control_set_midi_program_data(const int32_t pluginId, const int32_t index, const int32_t bank, const int32_t program, const char* const name)
{
    qDebug("CarlaEngine::osc_send_control_set_midi_program_data(%i, %i, %i, %i, \"%s\")", pluginId, index, bank, program, name);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->maxPluginNumber);
    CARLA_ASSERT(index >= 0);
    CARLA_ASSERT(bank >= 0);
    CARLA_ASSERT(program >= 0);
    CARLA_ASSERT(name);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+23];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_midi_program_data");
        lo_send(kData->oscData->target, target_path, "iiiis", pluginId, index, bank, program, name);
    }
}

void CarlaEngine::osc_send_control_note_on(const int32_t pluginId, const int32_t channel, const int32_t note, const int32_t velo)
{
    qDebug("CarlaEngine::osc_send_control_note_on(%i, %i, %i, %i)", pluginId, channel, note, velo);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->maxPluginNumber);
    CARLA_ASSERT(channel >= 0 && channel < 16);
    CARLA_ASSERT(note >= 0 && note < 128);
    CARLA_ASSERT(velo > 0 && velo < 128);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+9];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/note_on");
        lo_send(kData->oscData->target, target_path, "iiii", pluginId, channel, note, velo);
    }
}

void CarlaEngine::osc_send_control_note_off(const int32_t pluginId, const int32_t channel, const int32_t note)
{
    qDebug("CarlaEngine::osc_send_control_note_off(%i, %i, %i)", pluginId, channel, note);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->maxPluginNumber);
    CARLA_ASSERT(channel >= 0 && channel < 16);
    CARLA_ASSERT(note >= 0 && note < 128);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+10];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/note_off");
        lo_send(kData->oscData->target, target_path, "iii", pluginId, channel, note);
    }
}

void CarlaEngine::osc_send_control_set_input_peak_value(const int32_t pluginId, const int32_t portId)
{
    //qDebug("CarlaEngine::osc_send_control_set_input_peak_value(%i, %i)", pluginId, portId);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->maxPluginNumber);
    CARLA_ASSERT(portId == 1 || portId == 2);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+22];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_input_peak_value");
        lo_send(kData->oscData->target, target_path, "iid", pluginId, portId, kData->plugins[pluginId].insPeak[portId-1]);
    }
}

void CarlaEngine::osc_send_control_set_output_peak_value(const int32_t pluginId, const int32_t portId)
{
    //qDebug("CarlaEngine::osc_send_control_set_output_peak_value(%i, %i)", pluginId, portId);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(pluginId >= 0 && pluginId < (int32_t)kData->maxPluginNumber);
    CARLA_ASSERT(portId == 1 || portId == 2);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+23];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/set_output_peak_value");
        lo_send(kData->oscData->target, target_path, "iid", pluginId, portId, kData->plugins[pluginId].outsPeak[portId-1]);
    }
}

void CarlaEngine::osc_send_control_exit()
{
    qDebug("CarlaEngine::osc_send_control_exit()");
    CARLA_ASSERT(kData->oscData);

    if (kData->oscData && kData->oscData->target)
    {
        char target_path[strlen(kData->oscData->path)+6];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/exit");
        lo_send(kData->oscData->target, target_path, "");
    }
}
#else
void CarlaEngine::osc_send_bridge_audio_count(const int32_t ins, const int32_t outs, const int32_t total)
{
    qDebug("CarlaEngine::osc_send_bridge_audio_count(%i, %i, %i)", ins, outs, total);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(total >= 0 && total >= ins + outs);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+20];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_audio_count");
        lo_send(kData->oscData->target, target_path, "iii", ins, outs, total);
    }
}

void CarlaEngine::osc_send_bridge_midi_count(const int32_t ins, const int32_t outs, const int32_t total)
{
    qDebug("CarlaEngine::osc_send_bridge_midi_count(%i, %i, %i)", ins, outs, total);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(total >= 0 && total >= ins + outs);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+19];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_midi_count");
        lo_send(kData->oscData->target, target_path, "iii", ins, outs, total);
    }
}

void CarlaEngine::osc_send_bridge_parameter_count(const int32_t ins, const int32_t outs, const int32_t total)
{
    qDebug("CarlaEngine::osc_send_bridge_parameter_count(%i, %i, %i)", ins, outs, total);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(total >= 0 && total >= ins + outs);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+24];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_parameter_count");
        lo_send(kData->oscData->target, target_path, "iii", ins, outs, total);
    }
}

void CarlaEngine::osc_send_bridge_program_count(const int32_t count)
{
    qDebug("CarlaEngine::osc_send_bridge_program_count(%i)", count);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(count >= 0);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+22];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_program_count");
        lo_send(kData->oscData->target, target_path, "i", count);
    }
}

void CarlaEngine::osc_send_bridge_midi_program_count(const int32_t count)
{
    qDebug("CarlaEngine::osc_send_bridge_midi_program_count(%i)", count);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(count >= 0);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+27];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_midi_program_count");
        lo_send(kData->oscData->target, target_path, "i", count);
    }
}

void CarlaEngine::osc_send_bridge_plugin_info(const int32_t category, const int32_t hints, const char* const name, const char* const label, const char* const maker, const char* const copyright, const int64_t uniqueId)
{
    qDebug("CarlaEngine::osc_send_bridge_plugin_info(%i, %i, \"%s\", \"%s\", \"%s\", \"%s\", " P_INT64 ")", category, hints, name, label, maker, copyright, uniqueId);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(name);
    CARLA_ASSERT(label);
    CARLA_ASSERT(maker);
    CARLA_ASSERT(copyright);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+20];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_plugin_info");
        lo_send(kData->oscData->target, target_path, "iissssh", category, hints, name, label, maker, copyright, uniqueId);
    }
}

void CarlaEngine::osc_send_bridge_parameter_info(const int32_t index, const char* const name, const char* const unit)
{
    qDebug("CarlaEngine::osc_send_bridge_parameter_info(%i, \"%s\", \"%s\")", index, name, unit);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(name);
    CARLA_ASSERT(unit);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+23];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_parameter_info");
        lo_send(kData->oscData->target, target_path, "iss", index, name, unit);
    }
}

void CarlaEngine::osc_send_bridge_parameter_data(const int32_t index, const int32_t type, const int32_t rindex, const int32_t hints, const int32_t midiChannel, const int32_t midiCC)
{
    qDebug("CarlaEngine::osc_send_bridge_parameter_data(%i, %i, %i, %i, %i, %i)", index, type, rindex, hints, midiChannel, midiCC);
    CARLA_ASSERT(kData->oscData != nullptr);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+23];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_parameter_data");
        lo_send(kData->oscData->target, target_path, "iiiiii", index, type, rindex, hints, midiChannel, midiCC);
    }
}

void CarlaEngine::osc_send_bridge_parameter_ranges(const int32_t index, const double def, const double min, const double max, const double step, const double stepSmall, const double stepLarge)
{
    qDebug("CarlaEngine::osc_send_bridge_parameter_ranges(%i, %g, %g, %g, %g, %g, %g)", index, def, min, max, step, stepSmall, stepLarge);
    CARLA_ASSERT(kData->oscData != nullptr);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+25];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_parameter_ranges");
        lo_send(kData->oscData->target, target_path, "idddddd", index, def, min, max, step, stepSmall, stepLarge);
    }
}

void CarlaEngine::osc_send_bridge_program_info(const int32_t index, const char* const name)
{
    qDebug("CarlaEngine::osc_send_bridge_program_info(%i, \"%s\")", index, name);
    CARLA_ASSERT(kData->oscData != nullptr);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+21];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_program_info");
        lo_send(kData->oscData->target, target_path, "is", index, name);
    }
}

void CarlaEngine::osc_send_bridge_midi_program_info(const int32_t index, const int32_t bank, const int32_t program, const char* const label)
{
    qDebug("CarlaEngine::osc_send_bridge_midi_program_info(%i, %i, %i, \"%s\")", index, bank, program, label);
    CARLA_ASSERT(kData->oscData != nullptr);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+26];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_midi_program_info");
        lo_send(kData->oscData->target, target_path, "iiis", index, bank, program, label);
    }
}

void CarlaEngine::osc_send_bridge_configure(const char* const key, const char* const value)
{
    qDebug("CarlaEngine::osc_send_bridge_configure(\"%s\", \"%s\")", key, value);
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(key);
    CARLA_ASSERT(value);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+18];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_configure");
        lo_send(kData->oscData->target, target_path, "ss", key, value);
    }
}

void CarlaEngine::osc_send_bridge_set_parameter_value(const int32_t index, const double value)
{
    qDebug("CarlaEngine::osc_send_bridge_set_parameter_value(%i, %g)", index, value);
    CARLA_ASSERT(kData->oscData != nullptr);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+28];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_set_parameter_value");
        lo_send(kData->oscData->target, target_path, "id", index, value);
    }
}

void CarlaEngine::osc_send_bridge_set_default_value(const int32_t index, const double value)
{
    qDebug("CarlaEngine::osc_send_bridge_set_default_value(%i, %g)", index, value);
    CARLA_ASSERT(kData->oscData != nullptr);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+26];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_set_default_value");
        lo_send(kData->oscData->target, target_path, "id", index, value);
    }
}

void CarlaEngine::osc_send_bridge_set_program(const int32_t index)
{
    qDebug("CarlaEngine::osc_send_bridge_set_program(%i)", index);
    CARLA_ASSERT(kData->oscData != nullptr);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+20];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_set_program");
        lo_send(kData->oscData->target, target_path, "i", index);
    }
}

void CarlaEngine::osc_send_bridge_set_midi_program(const int32_t index)
{
    qDebug("CarlaEngine::osc_send_bridge_set_midi_program(%i)", index);
    CARLA_ASSERT(kData->oscData != nullptr);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+25];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_set_midi_program");
        lo_send(kData->oscData->target, target_path, "i", index);
    }
}

void CarlaEngine::osc_send_bridge_set_custom_data(const char* const type, const char* const key, const char* const value)
{
    qDebug("CarlaEngine::osc_send_bridge_set_custom_data(\"%s\", \"%s\", \"%s\")", type, key, value);
    CARLA_ASSERT(kData->oscData);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+24];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_set_custom_data");
        lo_send(kData->oscData->target, target_path, "sss", type, key, value);
    }
}

void CarlaEngine::osc_send_bridge_set_chunk_data(const char* const chunkFile)
{
    qDebug("CarlaEngine::osc_send_bridge_set_chunk_data(\"%s\")", chunkFile);
    CARLA_ASSERT(kData->oscData != nullptr);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+23];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_set_chunk_data");
        lo_send(kData->oscData->target, target_path, "s", chunkFile);
    }
}

#if 0
void CarlaEngine::osc_send_bridge_set_inpeak(const int32_t portId)
{
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(portId == 1 || portId == 2);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+28];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_set_inpeak");
        lo_send(kData->oscData->target, target_path, "id", portId, data->insPeak[portId-1]);
    }
}

void CarlaEngine::osc_send_bridge_set_outpeak(const int32_t portId)
{
    CARLA_ASSERT(kData->oscData != nullptr);
    CARLA_ASSERT(portId == 1 || portId == 2);

    if (kData->oscData != nullptr && kData->oscData->target != nullptr)
    {
        char target_path[strlen(kData->oscData->path)+29];
        strcpy(target_path, kData->oscData->path);
        strcat(target_path, "/bridge_set_outpeak");
        lo_send(kData->oscData->target, target_path, "id", portId, data->insPeak[portId-1]);
    }
}
#endif
#endif

CARLA_BACKEND_END_NAMESPACE
