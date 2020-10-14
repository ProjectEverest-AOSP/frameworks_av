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

// Unit Test for MediaTranscodingService.

//#define LOG_NDEBUG 0
#define LOG_TAG "MediaTranscodingServiceRealTest"

#include "MediaTranscodingServiceTestHelper.h"

/*
 * Tests media transcoding service with real transcoder.
 *
 * Uses the same test assets as the MediaTranscoder unit tests. Before running the test,
 * please make sure to push the test assets to /sdcard:
 *
 * adb push $TOP/frameworks/av/media/libmediatranscoding/transcoder/tests/assets /data/local/tmp/TranscodingTestAssets
 */
namespace android {

namespace media {

constexpr int64_t kPaddingUs = 400000;
constexpr int64_t kSessionWithPaddingUs = 10000000 + kPaddingUs;
constexpr int32_t kBitRate = 8 * 1000 * 1000;  // 8Mbs

constexpr const char* kShortSrcPath =
        "/data/local/tmp/TranscodingTestAssets/cubicle_avc_480x240_aac_24KHz.mp4";
constexpr const char* kLongSrcPath = "/data/local/tmp/TranscodingTestAssets/longtest_15s.mp4";

#define OUTPATH(name) "/data/local/tmp/MediaTranscodingService_" #name ".MP4"

class MediaTranscodingServiceRealTest : public MediaTranscodingServiceTestBase {
public:
    MediaTranscodingServiceRealTest() { ALOGI("MediaTranscodingServiceResourceTest created"); }

    virtual ~MediaTranscodingServiceRealTest() {
        ALOGI("MediaTranscodingServiceResourceTest destroyed");
    }
};

TEST_F(MediaTranscodingServiceRealTest, TestInvalidSource) {
    registerMultipleClients();

    const char* srcPath = "bad_file_uri";
    const char* dstPath = OUTPATH(TestInvalidSource);
    deleteFile(dstPath);

    // Submit one session.
    EXPECT_TRUE(
            mClient1->submit(0, srcPath, dstPath, TranscodingSessionPriority::kNormal, kBitRate));

    // Check expected error.
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Failed(CLIENT(1), 0));
    EXPECT_EQ(mClient1->getLastError(), TranscodingErrorCode::kErrorIO);

    unregisterMultipleClients();
}

TEST_F(MediaTranscodingServiceRealTest, TestPassthru) {
    registerMultipleClients();

    const char* dstPath = OUTPATH(TestPassthru);
    deleteFile(dstPath);

    // Submit one session.
    EXPECT_TRUE(mClient1->submit(0, kShortSrcPath, dstPath));

    // Wait for session to finish.
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Start(CLIENT(1), 0));
    EXPECT_EQ(mClient1->pop(kSessionWithPaddingUs), EventTracker::Finished(CLIENT(1), 0));

    unregisterMultipleClients();
}

TEST_F(MediaTranscodingServiceRealTest, TestTranscodeVideo) {
    registerMultipleClients();

    const char* dstPath = OUTPATH(TestTranscodeVideo);
    deleteFile(dstPath);

    // Submit one session.
    EXPECT_TRUE(mClient1->submit(0, kShortSrcPath, dstPath, TranscodingSessionPriority::kNormal,
                                 kBitRate));

    // Wait for session to finish.
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Start(CLIENT(1), 0));
    EXPECT_EQ(mClient1->pop(kSessionWithPaddingUs), EventTracker::Finished(CLIENT(1), 0));

    unregisterMultipleClients();
}

TEST_F(MediaTranscodingServiceRealTest, TestTranscodeVideoProgress) {
    registerMultipleClients();

    const char* dstPath = OUTPATH(TestTranscodeVideoProgress);
    deleteFile(dstPath);

    // Submit one session.
    EXPECT_TRUE(mClient1->submit(0, kLongSrcPath, dstPath, TranscodingSessionPriority::kNormal,
                                 kBitRate));

    // Wait for session to finish.
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Start(CLIENT(1), 0));
    EXPECT_EQ(mClient1->pop(kSessionWithPaddingUs), EventTracker::Finished(CLIENT(1), 0));

    // Check the progress update messages are received. For this clip (around ~15 second long),
    // expect at least 10 updates, and the last update should be 100.
    int lastProgress;
    EXPECT_GE(mClient1->getUpdateCount(&lastProgress), 10);
    EXPECT_EQ(lastProgress, 100);

    unregisterMultipleClients();
}

/*
 * Test cancel immediately after start.
 */
TEST_F(MediaTranscodingServiceRealTest, TestCancelImmediately) {
    registerMultipleClients();

    const char* srcPath0 = kLongSrcPath;
    const char* srcPath1 = kShortSrcPath;
    const char* dstPath0 = OUTPATH(TestCancelImmediately_Session0);
    const char* dstPath1 = OUTPATH(TestCancelImmediately_Session1);

    deleteFile(dstPath0);
    deleteFile(dstPath1);
    // Submit one session, should start immediately.
    EXPECT_TRUE(
            mClient1->submit(0, srcPath0, dstPath0, TranscodingSessionPriority::kNormal, kBitRate));
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Start(CLIENT(1), 0));
    EXPECT_TRUE(mClient1->getSession(0, srcPath0, dstPath0));

    // Test cancel session immediately, getSession should fail after cancel.
    EXPECT_TRUE(mClient1->cancel(0));
    EXPECT_TRUE(mClient1->getSession<fail>(0, "", ""));

    // Submit new session, new session should start immediately and finish.
    EXPECT_TRUE(
            mClient1->submit(1, srcPath1, dstPath1, TranscodingSessionPriority::kNormal, kBitRate));
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Start(CLIENT(1), 1));
    EXPECT_EQ(mClient1->pop(kSessionWithPaddingUs), EventTracker::Finished(CLIENT(1), 1));

    unregisterMultipleClients();
}

/*
 * Test cancel in the middle of transcoding.
 */
TEST_F(MediaTranscodingServiceRealTest, TestCancelWhileRunning) {
    registerMultipleClients();

    const char* srcPath0 = kLongSrcPath;
    const char* srcPath1 = kShortSrcPath;
    const char* dstPath0 = OUTPATH(TestCancelWhileRunning_Session0);
    const char* dstPath1 = OUTPATH(TestCancelWhileRunning_Session1);

    deleteFile(dstPath0);
    deleteFile(dstPath1);
    // Submit two sessions, session 0 should start immediately, session 1 should be queued.
    EXPECT_TRUE(
            mClient1->submit(0, srcPath0, dstPath0, TranscodingSessionPriority::kNormal, kBitRate));
    EXPECT_TRUE(
            mClient1->submit(1, srcPath1, dstPath1, TranscodingSessionPriority::kNormal, kBitRate));
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Start(CLIENT(1), 0));
    EXPECT_TRUE(mClient1->getSession(0, srcPath0, dstPath0));
    EXPECT_TRUE(mClient1->getSession(1, srcPath1, dstPath1));

    // Session 0 (longtest) shouldn't finish in 1 seconds.
    EXPECT_EQ(mClient1->pop(1000000), EventTracker::NoEvent);

    // Now cancel session 0. Session 1 should start immediately and finish.
    EXPECT_TRUE(mClient1->cancel(0));
    EXPECT_TRUE(mClient1->getSession<fail>(0, "", ""));
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Start(CLIENT(1), 1));
    EXPECT_EQ(mClient1->pop(kSessionWithPaddingUs), EventTracker::Finished(CLIENT(1), 1));

    unregisterMultipleClients();
}

TEST_F(MediaTranscodingServiceRealTest, TestPauseResumeSingleClient) {
    registerMultipleClients();

    const char* srcPath0 = kLongSrcPath;
    const char* srcPath1 = kShortSrcPath;
    const char* dstPath0 = OUTPATH(TestPauseResumeSingleClient_Session0);
    const char* dstPath1 = OUTPATH(TestPauseResumeSingleClient_Session1);
    deleteFile(dstPath0);
    deleteFile(dstPath1);

    // Submit one offline session, should start immediately.
    EXPECT_TRUE(mClient1->submit(0, srcPath0, dstPath0, TranscodingSessionPriority::kUnspecified,
                                 kBitRate));
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Start(CLIENT(1), 0));
    // Test get session after starts.
    EXPECT_TRUE(mClient1->getSession(0, srcPath0, dstPath0));

    // Submit one realtime session.
    EXPECT_TRUE(
            mClient1->submit(1, srcPath1, dstPath1, TranscodingSessionPriority::kNormal, kBitRate));

    // Offline session should pause.
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Pause(CLIENT(1), 0));
    EXPECT_TRUE(mClient1->getSession(0, srcPath0, dstPath0));

    // Realtime session should start immediately, and run to finish.
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Start(CLIENT(1), 1));
    EXPECT_EQ(mClient1->pop(kSessionWithPaddingUs), EventTracker::Finished(CLIENT(1), 1));

    // Test get session after finish fails.
    EXPECT_TRUE(mClient1->getSession<fail>(1, "", ""));

    // Then offline session should resume.
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Resume(CLIENT(1), 0));
    // Test get session after resume.
    EXPECT_TRUE(mClient1->getSession(0, srcPath0, dstPath0));

    // Offline session should finish.
    EXPECT_EQ(mClient1->pop(kSessionWithPaddingUs), EventTracker::Finished(CLIENT(1), 0));
    // Test get session after finish fails.
    EXPECT_TRUE(mClient1->getSession<fail>(0, "", ""));

    unregisterMultipleClients();
}

/*
 * Basic test for pause/resume with two clients, with one session each.
 * Top app's session should preempt the other app's session.
 */
TEST_F(MediaTranscodingServiceRealTest, TestPauseResumeMultiClients) {
    ALOGD("TestPauseResumeMultiClients starting...");

    EXPECT_TRUE(ShellHelper::RunCmd("input keyevent KEYCODE_WAKEUP"));
    EXPECT_TRUE(ShellHelper::RunCmd("wm dismiss-keyguard"));
    EXPECT_TRUE(ShellHelper::Stop(kClientPackageA));
    EXPECT_TRUE(ShellHelper::Stop(kClientPackageB));
    EXPECT_TRUE(ShellHelper::Stop(kClientPackageC));

    registerMultipleClients();

    const char* srcPath0 = kLongSrcPath;
    const char* srcPath1 = kShortSrcPath;
    const char* dstPath0 = OUTPATH(TestPauseResumeMultiClients_Client0);
    const char* dstPath1 = OUTPATH(TestPauseResumeMultiClients_Client1);
    deleteFile(dstPath0);
    deleteFile(dstPath1);

    ALOGD("Moving app A to top...");
    EXPECT_TRUE(ShellHelper::Start(kClientPackageA, kTestActivityName));

    // Submit session to Client1.
    ALOGD("Submitting session to client1 (app A) ...");
    EXPECT_TRUE(
            mClient1->submit(0, srcPath0, dstPath0, TranscodingSessionPriority::kNormal, kBitRate));

    // Client1's session should start immediately.
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Start(CLIENT(1), 0));

    ALOGD("Moving app B to top...");
    EXPECT_TRUE(ShellHelper::Start(kClientPackageB, kTestActivityName));

    // Client1's session should continue to run, since Client2 (app B) doesn't have any session.
    EXPECT_EQ(mClient1->pop(1000000), EventTracker::NoEvent);

    // Submit session to Client2.
    ALOGD("Submitting session to client2 (app B) ...");
    EXPECT_TRUE(
            mClient2->submit(0, srcPath1, dstPath1, TranscodingSessionPriority::kNormal, kBitRate));

    // Client1's session should pause, client2's session should start.
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Pause(CLIENT(1), 0));
    EXPECT_EQ(mClient2->pop(kPaddingUs), EventTracker::Start(CLIENT(2), 0));

    // Client2's session should finish, then Client1's session should resume.
    EXPECT_EQ(mClient2->pop(kSessionWithPaddingUs), EventTracker::Finished(CLIENT(2), 0));
    EXPECT_EQ(mClient1->pop(kPaddingUs), EventTracker::Resume(CLIENT(1), 0));

    // Client1's session should finish.
    EXPECT_EQ(mClient1->pop(kSessionWithPaddingUs), EventTracker::Finished(CLIENT(1), 0));

    unregisterMultipleClients();

    EXPECT_TRUE(ShellHelper::Stop(kClientPackageA));
    EXPECT_TRUE(ShellHelper::Stop(kClientPackageB));
    EXPECT_TRUE(ShellHelper::Stop(kClientPackageC));

    ALOGD("TestPauseResumeMultiClients finished.");
}

}  // namespace media
}  // namespace android
