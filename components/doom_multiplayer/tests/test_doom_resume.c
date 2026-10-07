// SPDX-License-Identifier: MIT
#include "p4/doom_resume.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const p4_doom_resume_record_t original = {
    .session_id=0x10203040, .self_peer_id=21, .host_peer_id=12,
    .session_seed=0x0102030405060708ULL, .slot=2, .player_count=4, .initial_player_mask=1,
    .ticket={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16},
    .compatibility_sha256={0xab,0xcd},
};

static void records(void)
{
    uint8_t bytes[P4_DOOM_RESUME_RECORD_BYTES];
    p4_doom_resume_record_t recovered;
    assert(p4_doom_resume_record_encode(&original,bytes));
    assert(!memcmp(bytes,"GCRT\2\2\4\1\x40\x30\x20\x10",12));
    assert(p4_doom_resume_record_decode(bytes,&recovered));
    assert(recovered.session_id==original.session_id && recovered.self_peer_id==21 &&
        recovered.host_peer_id==12 && recovered.slot==2 && recovered.player_count==4 &&
        recovered.session_seed==original.session_seed && recovered.initial_player_mask==1);
    assert(!memcmp(recovered.ticket,original.ticket,16));
    assert(!memcmp(recovered.compatibility_sha256,original.compatibility_sha256,32));
    /* Uninitialized/corrupted retained memory must never restore an identity. */
    for (unsigned i=0;i<sizeof(bytes);++i) {
        bytes[i]^=1;
        assert(!p4_doom_resume_record_decode(bytes,&recovered));
        bytes[i]^=1;
    }
    p4_doom_resume_record_t invalid=original; invalid.self_peer_id=invalid.host_peer_id;
    assert(!p4_doom_resume_record_encode(&invalid,bytes));
    invalid=original; memset(invalid.ticket,0,16);
    assert(!p4_doom_resume_record_encode(&invalid,bytes));
    invalid=original; invalid.slot=4;
    assert(!p4_doom_resume_record_encode(&invalid,bytes));
}

static void controls(void)
{
    p4_doom_resume_control_t value={.slot=2,.player_count=4,.reason=3,.initial_player_mask=1,
        .nonce=0xf0e0d0c0b0a09080ULL,
        .accept={.assigned_player_slot=2,.player_count=4,.input_delay_tics=2,
            .session_seed=0x0102030405060708ULL}};
    memcpy(value.ticket,original.ticket,16);
    memcpy(value.compatibility_sha256,original.compatibility_sha256,32);
    const size_t lengths[]={0,24,24,64,56,32};
    for (unsigned type=1;type<=5;++type) {
        value.type=(p4_doom_resume_type_t)type;
        uint8_t bytes[64]; size_t length=0;
        assert(p4_doom_resume_control_encode(&value,bytes,&length));
        assert(length==lengths[type] && !memcmp(bytes,"GCR2",4) && bytes[4]==type);
        assert(bytes[5]==2 && bytes[6]==(type==5?3:4) && bytes[7]==1);
        assert(!memcmp(bytes+8,original.ticket,16));
        p4_doom_resume_control_t result;
        assert(p4_doom_resume_control_decode(bytes,length,&result));
        assert(result.type==value.type && result.slot==2 && result.initial_player_mask==1 &&
            !memcmp(result.ticket,value.ticket,16));
        if (length>24) assert(result.nonce==value.nonce);
        if (type==3) assert(!memcmp(bytes+32,original.compatibility_sha256,32));
        if (type==4) assert(result.accept.session_seed==original.session_seed &&
            result.accept.assigned_player_slot==2 && result.accept.player_count==4);
        for (size_t truncated=0;truncated<length;++truncated)
            assert(!p4_doom_resume_control_decode(bytes,truncated,&result));
        assert(!p4_doom_resume_control_decode(bytes,length+1,&result));
        bytes[7]=0; assert(!p4_doom_resume_control_decode(bytes,length,&result)); bytes[7]=1;
        bytes[3]='1'; assert(!p4_doom_resume_control_decode(bytes,length,&result)); bytes[3]='2';
        bytes[4]=9; assert(!p4_doom_resume_control_decode(bytes,length,&result));
    }
    uint8_t bytes[64]; size_t length=0;
    value.type=P4_DOOM_RESUME_ACCEPTED; value.accept.assigned_player_slot=1;
    assert(!p4_doom_resume_control_encode(&value,bytes,&length));
    value.accept.assigned_player_slot=2; value.accept.start_tic=400;
    assert(!p4_doom_resume_control_encode(&value,bytes,&length));
    value.type=P4_DOOM_RESUME_REQUEST; value.nonce=0;
    assert(!p4_doom_resume_control_encode(&value,bytes,&length));
}

static void refresh_record_crc(uint8_t bytes[P4_DOOM_RESUME_RECORD_BYTES])
{
    const uint32_t crc=p4_mp_crc32(bytes,76);
    for (unsigned i=0;i<4;++i) bytes[76+i]=(uint8_t)(crc>>(8U*i));
}

static void record_mask_matrix(void)
{
    uint8_t guarded[P4_DOOM_RESUME_RECORD_BYTES+2];
    uint8_t *const bytes=guarded+1;
    p4_doom_resume_record_t decoded;
    for (unsigned count=2;count<=4;++count) {
        p4_doom_resume_record_t value=original;
        value.player_count=(uint8_t)count;
        value.slot=(uint8_t)(count-1U);
        for (unsigned mask=0;mask<=UINT8_MAX;++mask) {
            const bool valid=(mask & 1U)!=0U && mask<(1U<<count);
            value.initial_player_mask=(uint8_t)mask;
            memset(guarded,0xa5,sizeof(guarded));
            assert(p4_doom_resume_record_encode(&value,bytes)==valid);
            assert(guarded[0]==0xa5 && guarded[sizeof(guarded)-1U]==0xa5);
            if (valid) {
                assert(p4_doom_resume_record_decode(bytes,&decoded));
                assert(decoded.initial_player_mask==mask && decoded.slot==value.slot &&
                    decoded.player_count==count);
            }
            value.initial_player_mask=1;
            assert(p4_doom_resume_record_encode(&value,bytes));
            bytes[7]=(uint8_t)mask;
            refresh_record_crc(bytes);
            /* Correct CRC must not make invalid history metadata acceptable. */
            assert(p4_doom_resume_record_decode(bytes,&decoded)==valid);
        }
        value.initial_player_mask=1;
        assert(p4_doom_resume_record_encode(&value,bytes));
        bytes[4]=1;
        refresh_record_crc(bytes);
        assert(!p4_doom_resume_record_decode(bytes,&decoded));
        bytes[7]=0; /* Also reject the exact version-one reserved-byte layout. */
        refresh_record_crc(bytes);
        assert(!p4_doom_resume_record_decode(bytes,&decoded));
    }
}

static void control_mask_matrix(void)
{
    p4_doom_resume_control_t value={.slot=1,.player_count=2,.reason=3,
        .nonce=42,.initial_player_mask=1,
        .accept={.assigned_player_slot=1,.player_count=2,.input_delay_tics=2,
            .session_seed=0x0102030405060708ULL}};
    memcpy(value.ticket,original.ticket,sizeof(value.ticket));
    memcpy(value.compatibility_sha256,original.compatibility_sha256,
        sizeof(value.compatibility_sha256));
    uint8_t guarded[P4_DOOM_RESUME_CONTROL_BYTES+2];
    uint8_t *const bytes=guarded+1;
    p4_doom_resume_control_t decoded;
    for (unsigned type=1;type<=5;++type) {
        value.type=(p4_doom_resume_type_t)type;
        for (unsigned count=2;count<=4;++count) {
            value.player_count=value.accept.player_count=(uint8_t)count;
            value.slot=value.accept.assigned_player_slot=(uint8_t)(count-1U);
            for (unsigned mask=0;mask<=UINT8_MAX;++mask) {
                const unsigned capacity=type==P4_DOOM_RESUME_UNAVAILABLE ? 4U : count;
                const bool valid=(mask & 1U)!=0U && mask<(1U<<capacity);
                value.initial_player_mask=(uint8_t)mask;
                size_t length=123;
                memset(guarded,0xa5,sizeof(guarded));
                assert(p4_doom_resume_control_encode(&value,bytes,&length)==valid);
                assert(guarded[0]==0xa5 && guarded[sizeof(guarded)-1U]==0xa5);
                if (valid) {
                    assert(length<=P4_DOOM_RESUME_CONTROL_BYTES);
                    assert(p4_doom_resume_control_decode(bytes,length,&decoded));
                    assert(decoded.initial_player_mask==mask && decoded.slot==value.slot);
                } else assert(length==0);
                value.initial_player_mask=1;
                assert(p4_doom_resume_control_encode(&value,bytes,&length));
                bytes[7]=(uint8_t)mask;
                assert(p4_doom_resume_control_decode(bytes,length,&decoded)==valid);
            }
        }
    }
    /* UNAVAILABLE replaces capacity with a reason; a decoder has count zero. */
    value.type=P4_DOOM_RESUME_UNAVAILABLE;
    value.player_count=0;
    value.initial_player_mask=9;
    size_t length=0;
    assert(p4_doom_resume_control_encode(&value,bytes,&length));
    assert(p4_doom_resume_control_decode(bytes,length,&decoded));
    assert(decoded.player_count==0 && decoded.reason==3 && decoded.initial_player_mask==9);
}

static void malformed_identity_fields(void)
{
    p4_doom_resume_control_t value={.slot=2,.player_count=4,.initial_player_mask=1,
        .reason=1,.nonce=42,.accept={.assigned_player_slot=2,.player_count=4,
            .input_delay_tics=2,.session_seed=21}};
    memcpy(value.ticket,original.ticket,sizeof(value.ticket));
    memcpy(value.compatibility_sha256,original.compatibility_sha256,
        sizeof(value.compatibility_sha256));
    p4_doom_resume_control_t decoded;
    uint8_t bytes[P4_DOOM_RESUME_CONTROL_BYTES], bad[P4_DOOM_RESUME_CONTROL_BYTES];
    for (unsigned type=1;type<=5;++type) {
        value.type=(p4_doom_resume_type_t)type;
        size_t length=0;
        assert(p4_doom_resume_control_encode(&value,bytes,&length));
        memcpy(bad,bytes,sizeof(bad));
        memset(bad+8,0,16);
        assert(!p4_doom_resume_control_decode(bad,length,&decoded));
        for (unsigned slot=0;slot<=4;slot+=4) {
            memcpy(bad,bytes,sizeof(bad)); bad[5]=(uint8_t)slot;
            assert(!p4_doom_resume_control_decode(bad,length,&decoded));
        }
        memcpy(bad,bytes,sizeof(bad)); bad[6]=0;
        assert(!p4_doom_resume_control_decode(bad,length,&decoded));
        if (type!=P4_DOOM_RESUME_UNAVAILABLE) {
            static const uint8_t bad_counts[]={1,5,UINT8_MAX};
            for (size_t i=0;i<sizeof(bad_counts);++i) {
                memcpy(bad,bytes,sizeof(bad)); bad[6]=bad_counts[i];
                assert(!p4_doom_resume_control_decode(bad,length,&decoded));
            }
        }
        if (length>=32) {
            memcpy(bad,bytes,sizeof(bad)); memset(bad+24,0,8);
            assert(!p4_doom_resume_control_decode(bad,length,&decoded));
        }
        if (type==P4_DOOM_RESUME_REQUEST) {
            memcpy(bad,bytes,sizeof(bad)); memset(bad+32,0,32);
            assert(!p4_doom_resume_control_decode(bad,length,&decoded));
        }
    }
    static const uint8_t bad_counts[]={0,1,5,UINT8_MAX};
    for (size_t i=0;i<sizeof(bad_counts);++i) {
        p4_doom_resume_record_t record=original, decoded_record;
        uint8_t record_bytes[P4_DOOM_RESUME_RECORD_BYTES];
        record.player_count=bad_counts[i];
        assert(!p4_doom_resume_record_encode(&record,record_bytes));
        assert(p4_doom_resume_record_encode(&original,record_bytes));
        record_bytes[6]=bad_counts[i]; refresh_record_crc(record_bytes);
        assert(!p4_doom_resume_record_decode(record_bytes,&decoded_record));
    }
}

static void custom_admission(void)
{
    p4_mp_session_t client;
    assert(p4_mp_session_client_start(&client,1,2,3,4,0,3000)==P4_MP_OK);
    const p4_mp_session_t before=client;
    assert(p4_mp_session_accept_host(&client,8,4,1,10)==P4_MP_BAD_IDENTITY);
    assert(!memcmp(&before,&client,sizeof(client)));
    assert(p4_mp_session_accept_host(&client,3,8,1,10)==P4_MP_ROUTE_MISMATCH);
    assert(!memcmp(&before,&client,sizeof(client)));
    assert(p4_mp_session_accept_host(&client,3,4,0,10)==P4_MP_INVALID_ARGUMENT);
    assert(!memcmp(&before,&client,sizeof(client)));
    client.peers[0].last_sequence=8;
    assert(p4_mp_session_accept_host(&client,3,4,8,10)==P4_MP_REPLAYED);
    assert(p4_mp_session_accept_host(&client,3,4,9,10)==P4_MP_OK);
    assert(client.state==P4_MP_SESSION_CONNECTED && client.peers[0].connected &&
        client.peers[0].player_slot==0 && client.peers[0].last_sequence==9 &&
        client.peers[0].last_seen_ms==10);
    assert(p4_mp_session_accept_host(&client,3,4,10,11)==P4_MP_INVALID_STATE);
}

int main(void)
{
    records(); controls(); record_mask_matrix(); control_mask_matrix();
    malformed_identity_fields(); custom_admission();
    puts("Arena resume GCR2: bounded controls, all initial-mask bytes, absent fresh slots, "
         "record v2 CRC, old-version rejection, exact pending host admission passed");
}
