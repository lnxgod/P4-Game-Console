// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstddef>
struct FakePower {uint8_t level=0;void setVibration(uint8_t v){level=v;}};
struct FakeSpeaker {unsigned sounds=0;const int16_t *last_data=nullptr;void setVolume(uint8_t){}void setAllChannelVolume(uint8_t){}void stop(){}void setChannelVolume(unsigned,uint8_t){}bool playRaw(const int16_t* data,size_t,uint32_t,bool,uint32_t,int,bool){last_data=data;++sounds;return true;}};
struct FakeM5 {FakePower Power;FakeSpeaker Speaker;};inline FakeM5 M5;
