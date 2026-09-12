// Puente de FMOD: redirige la version UWP de FMOD a la fmod.dll de escritorio.
//
// Original: sub_62952DE0 (carga) y la treintena de envoltorios de sub_62951940
// en adelante.
//
// El juego UWP enlaza FMOD por importacion retardada. El shim sustituye esos
// huecos de la IAT por estos envoltorios, que cargan fmod.dll de escritorio y
// reenvian. Los mensajes de traza del original hablan de "25 FMOD delay-IAT
// slots" y "31 FMOD delay-IAT slots" segun la version del juego.
//
// Diseno clave: OBJETOS SILENCIOSOS.
//
// Si fmod.dll no carga o le falta algun export, el puente no falla. Igual que
// el DLL original, mantiene centinelas distintos para System, ChannelGroup,
// Sound y Channel/ChannelControl. Asi Minecraft puede conservar punteros no
// nulos del tipo que esperaba y las llamadas posteriores responden FMOD_OK sin
// desreferenciar objetos falsos.

#pragma once

#include "shim/common.h"

namespace shim::fmod {

// Codigo de retorno de FMOD. Solo se usa FMOD_OK; el resto se propaga tal cual
// desde la DLL real.
using Result = int;
inline constexpr Result kOk = 0;

// Punteros opacos a los objetos de FMOD. El shim nunca los desreferencia: solo
// los pasa de vuelta a la DLL real o los compara con el centinela.
using System = void;
using Sound = void;
using ChannelGroup = void;
using Channel = void;

// ---------------------------------------------------------------------------
// Envoltorios de la IAT retardada
//
// Son las funciones que se instalan en los huecos de importacion retardada de
// FMOD del juego. Todas siguen el mismo patron: si el objeto es el centinela o
// el puente no esta operativo, responden en modo silencioso; si no, reenvian.
//
// La firma de cada una debe coincidir exactamente con la del metodo de FMOD al
// que sustituye, incluida la convencion __stdcall.
// ---------------------------------------------------------------------------

namespace bridge {

Result __stdcall System_Create(System** system) noexcept;
Result __stdcall getVersion(System* system, unsigned int* version) noexcept;
Result __stdcall init(System* system, int max_channels, unsigned int flags,
                      void* extra_driver_data) noexcept;
Result __stdcall close(System* system) noexcept;
Result __stdcall System_release(System* system) noexcept;
Result __stdcall update(System* system) noexcept;
Result __stdcall mixerSuspend(System* system) noexcept;
Result __stdcall mixerResume(System* system) noexcept;

Result __stdcall getNumDrivers(System* system, int* count) noexcept;
Result __stdcall getDriverInfo(System* system, int index, char* name,
                               int name_length, void* guid, int* rate,
                               int* speaker_mode,
                               int* speaker_mode_channels) noexcept;
Result __stdcall setDriver(System* system, int driver) noexcept;
Result __stdcall setOutput(System* system, int output) noexcept;

Result __stdcall set3DSettings(System* system, float doppler_scale,
                               float distance_factor,
                               float rolloff_scale) noexcept;
Result __stdcall set3DListenerAttributes(System* system, int listener,
                                         const void* position,
                                         const void* velocity,
                                         const void* forward,
                                         const void* up) noexcept;

Result __stdcall createChannelGroup(System* system, const char* name,
                                    ChannelGroup** group) noexcept;
Result __stdcall getMasterChannelGroup(System* system,
                                       ChannelGroup** group) noexcept;
Result __stdcall addGroup(ChannelGroup* group, ChannelGroup* child,
                          bool propagate_clock, void** connection) noexcept;

Result __stdcall createSound(System* system, const char* name_or_data,
                             unsigned int mode, void* extra_info,
                             Sound** sound) noexcept;
Result __stdcall createStream(System* system, const char* name_or_data,
                              unsigned int mode, void* extra_info,
                              Sound** sound) noexcept;
Result __stdcall Sound_release(Sound* sound) noexcept;
Result __stdcall set3DMinMaxDistance(Sound* sound, float min,
                                     float max) noexcept;
Result __stdcall getNumSubSounds(Sound* sound, int* count) noexcept;
Result __stdcall getSubSound(Sound* sound, int index, Sound** sub) noexcept;
Result __stdcall playSound(System* system, Sound* sound, ChannelGroup* group,
                           bool paused, Channel** channel) noexcept;

Result __stdcall setMute(void* control, bool mute) noexcept;
Result __stdcall setVolume(void* control, float volume) noexcept;
Result __stdcall setPitch(void* control, float pitch) noexcept;
Result __stdcall setPaused(void* control, bool paused) noexcept;
Result __stdcall isPlaying(void* control, bool* playing) noexcept;
Result __stdcall stop(void* control) noexcept;
Result __stdcall set3DAttributes(void* control, const void* position,
                                 const void* velocity,
                                 const void* alt_pan_pos) noexcept;

}  // namespace bridge

}  // namespace shim::fmod
