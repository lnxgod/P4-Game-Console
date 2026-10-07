/* SPDX-License-Identifier: MIT
 * Isolated host proof of actual staged P4MP framing, not a network/device test.
 */
#include "p4/multiplayer.h"
#include "p4/doom_checkpoint_transfer.h"
#include "checkpoint_host_sha.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned assertions, groups;
#define CHECK(e) do { ++assertions; if (!(e)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); \
} } while (0)
#define RUN(f) do { (f)(); ++groups; printf("PASS %s\n", #f); } while (0)

_Static_assert(P4_MP_GAME_MESSAGE_MAX_BYTES == 64, "native payload unchanged");
_Static_assert(P4_MP_CHECKPOINT_MAX_PAYLOAD_BYTES == 992, "checkpoint payload");
_Static_assert(P4_MP_CHECKPOINT_MAX_DATAGRAM_BYTES == 1024, "transport bound");
_Static_assert(P4_CT_META_WIRE == 116, "metadata contract");
_Static_assert(P4_CT_PACKET_MAX == 940, "chunk contract");

static void put16(uint8_t *p, uint16_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8U); }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i=0; i<4; ++i) p[i]=(uint8_t)(v>>(8U*i)); }
static void refresh_crc(uint8_t *p, size_t n)
{ CHECK(n>=P4_MP_TRAILER_BYTES); put32(p+n-P4_MP_TRAILER_BYTES,p4_mp_crc32(p,n-P4_MP_TRAILER_BYTES)); }
static size_t frame(p4_mp_packet_type_t type, const uint8_t *p, size_t n,
                    uint8_t out[P4_MP_MAX_DATAGRAM_BYTES])
{
    size_t bytes=0;
    CHECK(n<=UINT16_MAX);
    CHECK(p4_mp_packet_encode(type,101,2,9,7,p,(uint16_t)n,out,
                              P4_MP_MAX_DATAGRAM_BYTES,&bytes)==P4_MP_OK);
    return bytes;
}
static void clear_decode(const uint8_t *p, size_t n, p4_mp_status_t expected)
{
    p4_mp_packet_view_t v;
    memset(&v,0xa5,sizeof v);
    CHECK(p4_mp_packet_decode(p,n,&v)==expected);
    CHECK(v.type==0 && v.payload==NULL && v.payload_length==0 && v.session_id==0 &&
          v.peer_id==0 && v.sequence==0 && v.ack==0 && v.flags==0);
}
static bool verify_sha(void *context, const uint8_t *p, size_t n, const uint8_t expected[32])
{
    (void)context;
    uint8_t digest[32];
    CHECK(n<=UINT_MAX);
    CHECK(p4_doom_checkpoint_sha256(p,n,digest));
    return memcmp(digest,expected,sizeof digest)==0;
}

static void actual_transfer_frames(void)
{
    uint8_t snapshot[P4_CT_CHUNK_BYTES];
    for (size_t i=0;i<sizeof snapshot;++i) snapshot[i]=(uint8_t)(i^0xa5U);
    p4_ct_meta meta={.identity={.schema=2,.session=101,.attempt=22,.checkpoint=33},
                     .length=sizeof snapshot,.next_tic=1234,.map=1,.members=1};
    memset(meta.identity.content_id,0x55,sizeof meta.identity.content_id);
    CHECK(p4_doom_checkpoint_sha256(snapshot,sizeof snapshot,meta.sha256));
    uint8_t inner[P4_CT_PACKET_MAX], outer[P4_MP_MAX_DATAGRAM_BYTES];
    size_t n=0;
    CHECK(p4_ct_meta_encode(&meta,inner,sizeof inner,&n));
    CHECK(n==P4_CT_META_WIRE);
    size_t bytes=frame(P4_MP_PACKET_CHECKPOINT,inner,n,outer);
    CHECK(bytes==148);
    p4_mp_packet_view_t v;
    CHECK(p4_mp_packet_decode(outer,bytes,&v)==P4_MP_OK);
    CHECK(v.type==P4_MP_PACKET_CHECKPOINT && v.payload_length==P4_CT_META_WIRE);
    CHECK(v.session_id==101 && v.peer_id==2 && v.sequence==9 && v.ack==7);
    p4_ct_meta decoded;
    CHECK(p4_ct_meta_decode(&decoded,v.payload,v.payload_length));
    CHECK(decoded.identity.attempt==22 && decoded.identity.checkpoint==33 &&
          decoded.next_tic==1234 && memcmp(decoded.sha256,meta.sha256,32)==0);

    p4_ct_tx tx={0}; p4_ct_rx rx={0}; uint8_t restored[sizeof snapshot];
    CHECK(p4_ct_tx_start(&tx,&meta,snapshot,sizeof snapshot,verify_sha,NULL,100));
    CHECK(p4_ct_rx_start(&rx,&meta.identity,&meta,restored,sizeof restored,verify_sha,NULL,100));
    uint32_t index=UINT32_MAX;
    CHECK(p4_ct_tx_prepare(&tx,100,inner,sizeof inner,&n,&index));
    CHECK(index==0 && n==P4_CT_PACKET_MAX);
    bytes=frame(P4_MP_PACKET_CHECKPOINT,inner,n,outer);
    CHECK(bytes==972 && bytes<=P4_MP_CHECKPOINT_MAX_DATAGRAM_BYTES);
    CHECK(p4_mp_packet_decode(outer,bytes,&v)==P4_MP_OK);
    CHECK(p4_ct_rx_chunk(&rx,v.payload,v.payload_length,101)==P4_CT_READY);
    CHECK(p4_ct_tx_sent(&tx,index,100));
    size_t restored_length=0;
    CHECK(p4_ct_rx_data(&rx,&restored_length)==restored && restored_length==sizeof snapshot);
    CHECK(memcmp(restored,snapshot,sizeof snapshot)==0);
    CHECK(p4_ct_rx_ack(&rx,inner,sizeof inner,&n));
    CHECK(n==P4_CT_ACK_WIRE);
    bytes=frame(P4_MP_PACKET_CHECKPOINT,inner,n,outer);
    CHECK(p4_mp_packet_decode(outer,bytes,&v)==P4_MP_OK);
    CHECK(p4_ct_tx_ack(&tx,v.payload,v.payload_length,102)==P4_CT_READY);
}

static void boundaries_and_damage(void)
{
    uint8_t payload[993], frame_bytes[P4_MP_MAX_DATAGRAM_BYTES];
    memset(payload,0x5a,sizeof payload);
    size_t n=frame(P4_MP_PACKET_CHECKPOINT,payload,992,frame_bytes);
    CHECK(n==1024);
    p4_mp_packet_view_t v;
    CHECK(p4_mp_packet_decode(frame_bytes,n,&v)==P4_MP_OK && v.payload_length==992);
    uint8_t copy[P4_MP_MAX_DATAGRAM_BYTES];
    memcpy(copy,frame_bytes,n); copy[100]^=1;
    clear_decode(copy,n,P4_MP_BAD_CRC);
    for (size_t cut=0;cut<n;++cut)
        clear_decode(frame_bytes,cut,cut<32?P4_MP_TOO_SHORT:P4_MP_BAD_LENGTH);
    memcpy(copy,frame_bytes,n); copy[6]=1; refresh_crc(copy,n);
    clear_decode(copy,n,P4_MP_BAD_FLAGS);
    memcpy(copy,frame_bytes,n); copy[26]=1; refresh_crc(copy,n);
    clear_decode(copy,n,P4_MP_BAD_FLAGS);
    memcpy(copy,frame_bytes,n); memset(copy+8,0,4); refresh_crc(copy,n);
    clear_decode(copy,n,P4_MP_BAD_IDENTITY);
    memcpy(copy,frame_bytes,n); memset(copy+12,0,4); refresh_crc(copy,n);
    clear_decode(copy,n,P4_MP_BAD_IDENTITY);
    memcpy(copy,frame_bytes,n); memset(copy+16,0,4); refresh_crc(copy,n);
    clear_decode(copy,n,P4_MP_BAD_IDENTITY);
    memcpy(copy,frame_bytes,n); copy[5]=14; refresh_crc(copy,n);
    clear_decode(copy,n,P4_MP_BAD_TYPE);
    memcpy(copy,frame_bytes,n); put16(copy+24,993); copy[n]=0;
    refresh_crc(copy,n+1); clear_decode(copy,n+1,P4_MP_BAD_LENGTH);
    memset(copy,0xa5,sizeof copy);
    size_t out=SIZE_MAX;
    CHECK(p4_mp_packet_encode(P4_MP_PACKET_CHECKPOINT,101,2,9,0,payload,993,
                              copy,sizeof copy,&out)==P4_MP_BAD_LENGTH && out==0);
    for (size_t i=0;i<sizeof copy;++i) CHECK(copy[i]==0xa5);
    CHECK(p4_mp_packet_encode(P4_MP_PACKET_CHECKPOINT,101,2,9,0,payload,0,
                              copy,sizeof copy,&out)==P4_MP_BAD_LENGTH && out==0);
    CHECK(p4_mp_packet_encode(P4_MP_PACKET_CHECKPOINT,101,2,9,0,payload,992,
                              copy,1023,&out)==P4_MP_BAD_LENGTH && out==0);
    n=frame(P4_MP_PACKET_GAME_MESSAGE,payload,64,frame_bytes);
    CHECK(p4_mp_packet_decode(frame_bytes,n,&v)==P4_MP_OK && v.payload_length==64);
    CHECK(p4_mp_packet_encode(P4_MP_PACKET_GAME_MESSAGE,101,2,9,0,payload,65,
                              copy,sizeof copy,&out)==P4_MP_BAD_LENGTH && out==0);
    /* A syntactically valid 65-byte checkpoint cannot disguise itself as native. */
    n=frame(P4_MP_PACKET_CHECKPOINT,payload,65,copy);
    copy[5]=P4_MP_PACKET_GAME_MESSAGE; refresh_crc(copy,n);
    clear_decode(copy,n,P4_MP_BAD_LENGTH);
    n=frame(P4_MP_PACKET_ACCESSORY,payload,48,copy);
    CHECK(p4_mp_packet_decode(copy,n,&v)==P4_MP_OK);
    CHECK(p4_mp_packet_encode(P4_MP_PACKET_ACCESSORY,101,2,9,0,payload,49,
                              copy,sizeof copy,&out)==P4_MP_BAD_LENGTH);
}

static void reject_unchanged(p4_mp_session_t *session, uint64_t route,
                           const uint8_t *bytes, size_t n)
{
    uint8_t before[sizeof *session]; memcpy(before,session,sizeof before);
    p4_mp_event_t event; memset(&event,0xa5,sizeof event);
    CHECK(p4_mp_session_receive(session,route,2000,bytes,n,&event)==P4_MP_BAD_TYPE);
    CHECK(memcmp(before,session,sizeof before)==0);
    CHECK(event.type==P4_MP_EVENT_NONE && event.peer_id==0 && event.route_id==0 &&
          event.packet.payload==NULL && event.packet.payload_length==0 && !event.neutralize_player);
}
static void generic_session_isolation(void)
{
    uint8_t payload[P4_CT_PACKET_MAX]={0}, datagram[P4_MP_MAX_DATAGRAM_BYTES];
    size_t n=frame(P4_MP_PACKET_CHECKPOINT,payload,sizeof payload,datagram);
    p4_mp_session_t host;
    CHECK(p4_mp_session_host_start(&host,101,1,3000)==P4_MP_OK);
    reject_unchanged(&host,0x1234,datagram,n); /* Unknown peer cannot be admitted. */
    CHECK(p4_mp_session_accept_peer(&host,2,0x1234,1,8,1000)==P4_MP_OK);
    reject_unchanged(&host,0x1234,datagram,n); /* Known connected peer. */
    reject_unchanged(&host,0x5678,datagram,n); /* Route mismatch. */
    datagram[8]^=1; refresh_crc(datagram,n);
    reject_unchanged(&host,0x1234,datagram,n); /* Other session. */
    datagram[8]^=1; datagram[12]=1; refresh_crc(datagram,n);
    p4_mp_session_t guest;
    CHECK(p4_mp_session_client_start(&guest,101,2,1,0x1234,1000,3000)==P4_MP_OK);
    reject_unchanged(&guest,0x1234,datagram,n); /* Pending cold guest. */
    CHECK(p4_mp_session_accept_host(&guest,1,0x1234,8,1000)==P4_MP_OK);
    reject_unchanged(&guest,0x1234,datagram,n); /* Admitted cold guest. */
    /* Dedicated outgoing caller may use the common sequence allocator. */
    uint32_t sequence=host.next_sequence;
    CHECK(p4_mp_session_encode(&host,P4_MP_PACKET_CHECKPOINT,99,payload,sizeof payload,
                               datagram,sizeof datagram,&n)==P4_MP_OK);
    p4_mp_packet_view_t v;
    CHECK(p4_mp_packet_decode(datagram,n,&v)==P4_MP_OK);
    CHECK(v.sequence==sequence && v.ack==99 && v.peer_id==1 &&
          host.next_sequence==sequence+1);
    /* Rejected checkpoint cannot steal the next legitimate input sequence. */
    p4_mp_input_t input={.tick=23,.buttons=1}; uint8_t input_bytes[P4_MP_INPUT_PAYLOAD_BYTES];
    p4_mp_input_encode(&input,input_bytes);
    n=frame(P4_MP_PACKET_INPUT,input_bytes,sizeof input_bytes,datagram);
    p4_mp_event_t event;
    CHECK(p4_mp_session_receive(&host,0x1234,2001,datagram,n,&event)==P4_MP_OK);
    CHECK(event.type==P4_MP_EVENT_INPUT && host.peers[0].last_sequence==9 &&
          host.peers[0].last_seen_ms==2001);
}

static void chunked_stream_frames(void)
{
    uint8_t payload[992]={0}, datagram[P4_MP_MAX_DATAGRAM_BYTES];
    size_t n=frame(P4_MP_PACKET_CHECKPOINT,payload,sizeof payload,datagram);
    for (size_t step=1;step<=65;step+=8) {
        p4_mp_stream_decoder_t decoder; p4_mp_stream_decoder_init(&decoder);
        uint8_t output[P4_MP_MAX_DATAGRAM_BYTES];
        size_t offset=0, total=0;
        while (offset<n) {
            size_t count=n-offset; if (count>step) count=step;
            size_t consumed=0;
            p4_mp_stream_result_t result=p4_mp_stream_consume(&decoder,datagram+offset,count,
                                                            &consumed,output,sizeof output,&total);
            CHECK(consumed==count); offset+=consumed;
            CHECK(result==(offset==n?P4_MP_STREAM_FRAME_READY:P4_MP_STREAM_NEED_MORE));
        }
        CHECK(total==n && memcmp(output,datagram,n)==0);
    }
}

static void dedicated_reject_unchanged(p4_mp_session_t *session, uint64_t route,
                                      const uint8_t *bytes, size_t n, p4_mp_status_t expected)
{
    uint8_t before[sizeof *session]; memcpy(before,session,sizeof before);
    p4_mp_event_t event; memset(&event,0xa5,sizeof event);
    CHECK(p4_mp_session_receive_checkpoint(session,route,9999,bytes,n,&event)==expected);
    CHECK(memcmp(before,session,sizeof before)==0);
    CHECK(event.type==P4_MP_EVENT_NONE && event.peer_id==0 && event.route_id==0 &&
          event.packet.payload==NULL && event.packet.payload_length==0 && !event.neutralize_player);
}
static void dedicated_ingress_gates(void)
{
    uint8_t payload[940]={0}, bytes[P4_MP_MAX_DATAGRAM_BYTES], altered[P4_MP_MAX_DATAGRAM_BYTES];
    size_t n=frame(P4_MP_PACKET_CHECKPOINT,payload,sizeof payload,bytes);
    p4_mp_session_t host;
    CHECK(p4_mp_session_host_start(&host,101,1,3000)==P4_MP_OK);
    CHECK(p4_mp_session_accept_peer(&host,2,0x1234,1,8,1000)==P4_MP_OK);
    dedicated_reject_unchanged(&host,0x5678,bytes,n,P4_MP_ROUTE_MISMATCH);
    memcpy(altered,bytes,n); put32(altered+8,102); refresh_crc(altered,n);
    dedicated_reject_unchanged(&host,0x1234,altered,n,P4_MP_WRONG_SESSION);
    memcpy(altered,bytes,n); put32(altered+12,1); refresh_crc(altered,n);
    dedicated_reject_unchanged(&host,0x1234,altered,n,P4_MP_BAD_IDENTITY);
    memcpy(altered,bytes,n); put32(altered+12,3); refresh_crc(altered,n);
    dedicated_reject_unchanged(&host,0x1234,altered,n,P4_MP_UNKNOWN_PEER);
    host.peers[0].connected=false;
    dedicated_reject_unchanged(&host,0x1234,bytes,n,P4_MP_UNKNOWN_PEER);
    host.peers[0].connected=true;
    memcpy(altered,bytes,n); altered[100]^=1;
    dedicated_reject_unchanged(&host,0x1234,altered,n,P4_MP_BAD_CRC);
    dedicated_reject_unchanged(&host,0x1234,bytes,n-1,P4_MP_BAD_LENGTH);
    size_t native=frame(P4_MP_PACKET_GAME_MESSAGE,payload,64,altered);
    dedicated_reject_unchanged(&host,0x1234,altered,native,P4_MP_BAD_TYPE);
    const p4_mp_session_state_t blocked[]={P4_MP_SESSION_IDLE,P4_MP_SESSION_HOSTING,
        P4_MP_SESSION_JOINING,P4_MP_SESSION_CLOSED};
    for (size_t i=0;i<sizeof blocked/sizeof blocked[0];++i) {
        host.state=blocked[i];
        dedicated_reject_unchanged(&host,0x1234,bytes,n,P4_MP_INVALID_STATE);
    }
    host.state=P4_MP_SESSION_CONNECTED;
    p4_mp_session_t expected=host;
    expected.peers[0].last_sequence=9; expected.peers[0].last_seen_ms=2000;
    p4_mp_event_t event;
    CHECK(p4_mp_session_receive_checkpoint(&host,0x1234,2000,bytes,n,&event)==P4_MP_OK);
    CHECK(memcmp(&host,&expected,sizeof host)==0);
    CHECK(event.type==P4_MP_EVENT_CHECKPOINT && event.peer_id==2 && event.route_id==0x1234 &&
          event.player_slot==1 && !event.neutralize_player && event.packet.type==P4_MP_PACKET_CHECKPOINT &&
          event.packet.payload==bytes+P4_MP_HEADER_BYTES && event.packet.payload_length==sizeof payload);
    dedicated_reject_unchanged(&host,0x1234,bytes,n,P4_MP_REPLAYED);
    const uint32_t rejected_sequences[]={8,9,UINT32_C(0x80000009)};
    for (size_t i=0;i<sizeof rejected_sequences/sizeof rejected_sequences[0];++i) {
        memcpy(altered,bytes,n); put32(altered+16,rejected_sequences[i]); refresh_crc(altered,n);
        dedicated_reject_unchanged(&host,0x1234,altered,n,P4_MP_REPLAYED);
    }
    /* Dedicated and ordinary ingress share one receive sequence. */
    p4_mp_input_t input={.tick=24,.buttons=2}; uint8_t input_bytes[P4_MP_INPUT_PAYLOAD_BYTES];
    p4_mp_input_encode(&input,input_bytes);
    native=frame(P4_MP_PACKET_INPUT,input_bytes,sizeof input_bytes,altered);
    CHECK(p4_mp_session_receive(&host,0x1234,2001,altered,native,&event)==P4_MP_REPLAYED);
    CHECK(host.peers[0].last_seen_ms==2000 && host.peers[0].last_sequence==9);
    put32(altered+16,10); refresh_crc(altered,native);
    CHECK(p4_mp_session_receive(&host,0x1234,2001,altered,native,&event)==P4_MP_OK);
    CHECK(event.type==P4_MP_EVENT_INPUT && host.peers[0].last_sequence==10);
    put32(bytes+16,10); refresh_crc(bytes,n);
    dedicated_reject_unchanged(&host,0x1234,bytes,n,P4_MP_REPLAYED);
    put32(bytes+16,11); refresh_crc(bytes,n);
    CHECK(p4_mp_session_receive_checkpoint(&host,0x1234,2002,bytes,n,&event)==P4_MP_OK);
    CHECK(event.type==P4_MP_EVENT_CHECKPOINT && host.peers[0].last_seen_ms==2002);

    /* A genuinely pending guest cannot accept checkpoints until admission. */
    p4_mp_session_t guest;
    CHECK(p4_mp_session_client_start(&guest,101,2,1,0x1234,1000,3000)==P4_MP_OK);
    put32(bytes+12,1); refresh_crc(bytes,n);
    dedicated_reject_unchanged(&guest,0x1234,bytes,n,P4_MP_INVALID_STATE);
    CHECK(p4_mp_session_accept_host(&guest,1,0x1234,10,1000)==P4_MP_OK);
    CHECK(p4_mp_session_receive_checkpoint(&guest,0x1234,2000,bytes,n,&event)==P4_MP_OK);
    CHECK(event.type==P4_MP_EVENT_CHECKPOINT && event.player_slot==0 &&
          guest.peers[0].last_sequence==11 && guest.peers[0].last_seen_ms==2000);

    /* Existing serial arithmetic accepts wrap to nonzero sequence 1. */
    CHECK(p4_mp_session_host_start(&host,101,1,3000)==P4_MP_OK);
    CHECK(p4_mp_session_accept_peer(&host,2,0x1234,1,UINT32_MAX,1000)==P4_MP_OK);
    put32(bytes+12,2); put32(bytes+16,1); refresh_crc(bytes,n);
    CHECK(p4_mp_session_receive_checkpoint(&host,0x1234,2000,bytes,n,&event)==P4_MP_OK);
    CHECK(host.peers[0].last_sequence==1);
    put32(bytes+16,UINT32_MAX); refresh_crc(bytes,n);
    dedicated_reject_unchanged(&host,0x1234,bytes,n,P4_MP_REPLAYED);
}

int main(void)
{
    RUN(actual_transfer_frames);
    RUN(boundaries_and_damage);
    RUN(generic_session_isolation);
    RUN(chunked_stream_frames);
    RUN(dedicated_ingress_gates);
    printf("PASS %u groups, %u assertions; staged P4MP checkpoint wire only\n",groups,assertions);
    return 0;
}
