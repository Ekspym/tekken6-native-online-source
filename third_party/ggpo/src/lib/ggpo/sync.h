/* -----------------------------------------------------------------------
 * GGPO.net (http://ggpo.net)  -  Copyright 2009 GroundStorm Studios, LLC.
 *
 * Use of this software is governed by the MIT license that can be found
 * in the LICENSE file.
 */

#ifndef _SYNC_H
#define _SYNC_H

#include "types.h"
#include "ggponet.h"
#include "game_input.h"
#include "input_queue.h"
#include "ring_buffer.h"
#include "network/udp_msg.h"

#define MAX_PREDICTION_FRAMES    6 // was 3 (PS3 rule: a re-simulated frame cost 10+ ms there). Native: ~1.5 ms; at 130 ms ping with delay 1 the window of 3 made the game wait every frame (43 fps, 2026-10-13). 6 + 2 = the 8 snapshots of kRing (t6_netplay.cpp)

class SyncTestBackend;

class Sync {
public:
   struct Config {
      GGPOSessionCallbacks    callbacks;
      int                     num_prediction_frames;
      int                     num_players;
      int                     input_size;
   };
   struct Event {
      enum {
         ConfirmedInput,
      } type;
      union {
         struct {
            GameInput   input;
         } confirmedInput;
      } u;
   };

public:
   Sync(UdpMsg::connect_status *connect_status);
   virtual ~Sync();

   void Init(Config &config);

   void SetLastConfirmedFrame(int frame);
   void SetFrameDelay(int queue, int delay);
   bool AddLocalInput(int queue, GameInput &input);
   void AddRemoteInput(int queue, GameInput &input);
   int GetConfirmedInputs(void *values, int size, int frame);
   int SynchronizeInputs(void *values, int size);

   void CheckSimulation(int timeout);
   void AdjustSimulation(int seek_to);
   void IncrementFrame(void);
   // tekken6-x360: the frame counter moves on without a save; the save follows after the poll when no rollback came
   // (p2p.cpp IncrementFrame): a state saved just before a rollback is thrown away by it.
   void IncrementFrameNoSave(void) { _framecount++; }
   void SaveNow(void) { SaveCurrentFrame(); }
   int RollbackCount(void) { return _rollbacks; }
   int _rollbacks = 0;

   int GetFrameCount() { return _framecount; }
   bool InRollback() { return _rollingback; }

   bool GetEvent(Event &e);

protected:
   friend SyncTestBackend;

   struct SavedFrame {
      byte    *buf;
      int      cbuf;
      int      frame;
      int      checksum;
      SavedFrame() : buf(NULL), cbuf(0), frame(-1), checksum(0) { }
   };
   struct SavedState {
      SavedFrame frames[MAX_PREDICTION_FRAMES + 2];
      int head;
   };

   void LoadFrame(int frame);
   void SaveCurrentFrame();
   int FindSavedFrameIndex(int frame);
   SavedFrame &GetLastSavedFrame();

   bool CreateQueues(Config &config);
   bool CheckSimulationConsistency(int *seekTo);
   void ResetPrediction(int frameNumber);

protected:
   GGPOSessionCallbacks _callbacks;
   SavedState     _savedstate;
   Config         _config;

   bool           _rollingback;
   int            _last_confirmed_frame;
   int            _framecount;
   int            _max_prediction_frames;

   InputQueue     *_input_queues;

   RingBuffer<Event, 32> _event_queue;
   UdpMsg::connect_status *_local_connect_status;
};

#endif

