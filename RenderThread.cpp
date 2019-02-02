#include "stdafx.h"
#include <process.h>
#include "config.h"

#include "RenderThread.h"

#include "AppInstance.h"
#include "TextureCache.h"
#include "Console.h"
#include "ScriptedCoverPositions.h"
#include "DisplayPosition.h"
#include "PlaybackTracer.h"
#include "MyActions.h"




void RenderThread::TargetChangedMessage::run(RenderThread& rt) {
	collection_read_lock lock(rt.appInstance);
	rt.displayPos.onTargetChange();
	rt.texCache.onTargetChange();
}

void RenderThread::RedrawMessage::run(RenderThread& rt) {
	rt.doPaint = true;
}

void RenderThread::DeviceModeMessage::run(RenderThread& rt) {
	rt.updateRefreshRate();
}

void RenderThread::WindowResizeMessage::run(RenderThread& rt, int width, int height) {
	rt.renderer.resizeGlScene(width, height);
}

void RenderThread::ChangeCPScriptMessage::run(RenderThread& rt, pfc::string8 script) {
	pfc::string8 tmp;
	rt.renderer.coverPos.setScript(script, tmp);
	rt.renderer.setProjectionMatrix();
	rt.doPaint = true;
}


void RenderThread::WindowHideMessage::run(RenderThread& rt) {
	//texCache.isPaused = true;
	if (cfgEmptyCacheOnMinimize) {
		rt.texCache.clearCache();
	}
}

void RenderThread::WindowShowMessage::run(RenderThread& rt) {
	//texCache.isPaused = false;
}

void RenderThread::TextFormatChangedMessage::run(RenderThread& rt) {
	rt.renderer.textDisplay.clearCache();
}

void RenderThread::CharEntered::run(RenderThread& rt, WPARAM wParam) {
	rt.findAsYouType.onChar(wParam);
}


void RenderThread::Run::run(RenderThread& rt, std::function<void()> f) {
	f();
}

void RenderThread::MoveToNowPlayingMessage::run(RenderThread& rt) {
	collection_read_lock lock(rt.appInstance);
	rt.appInstance->playbackTracer->moveToNowPlaying();
}

void RenderThread::MoveTargetMessage::run(RenderThread& rt, int moveBy, bool moveToEnd) {
	collection_read_lock lock(rt.appInstance);
	if (!rt.appInstance->albumCollection->getCount())
		return;
	
	if (!moveToEnd) {
		rt.appInstance->albumCollection->moveTargetBy(moveBy);
		rt.appInstance->playbackTracer->userStartedMovement();
	} else {
		CollectionPos newTarget;
		if (moveBy > 0) {
			newTarget = --rt.appInstance->albumCollection->end();
		} else {
			newTarget = rt.appInstance->albumCollection->begin();
		}
		rt.appInstance->albumCollection->setTargetPos(newTarget);
		rt.appInstance->playbackTracer->userStartedMovement();
	}
}

void RenderThread::MoveToTrack::run(RenderThread& rt, metadb_handle_ptr track) {
	collection_read_lock lock(rt.appInstance);
	CollectionPos target;
	if (rt.appInstance->albumCollection->getAlbumForTrack(track, target)) {
		rt.appInstance->albumCollection->setTargetPos(target);
	}
}



void RenderThread::MoveToAlbumMessage::run(RenderThread& rt, std::string groupString) {
	collection_read_lock lock(rt.appInstance);
	if (!rt.appInstance->albumCollection->getCount())
		return;

	rt.appInstance->albumCollection->setTargetByName(groupString);
	rt.appInstance->playbackTracer->userStartedMovement();
}

std::optional<AlbumInfo> RenderThread::GetAlbumAtCoords::run(RenderThread& rt, int x, int y) {
	collection_read_lock lock(rt.appInstance);
	int offset;
	if (!rt.renderer.offsetOnPoint(x, y, offset)) {
		return std::nullopt;
	}
	CollectionPos pos = rt.displayPos.getOffsetPos(offset);
	return rt.appInstance->albumCollection->getAlbumInfo(pos);
}

std::optional<AlbumInfo> RenderThread::GetTargetAlbum::run(RenderThread& rt)
{
	collection_read_lock lock(rt.appInstance);
	if (!rt.appInstance->albumCollection->getCount())
		return std::nullopt;
	auto pos = rt.appInstance->albumCollection->getTargetPos();
	return rt.appInstance->albumCollection->getAlbumInfo(pos);
}

void RenderThread::ReloadCollectionMessage::run(RenderThread& rt) {
	rt.appInstance->startCollectionReload();
}

void RenderThread::CollectionReloadedMessage::run(RenderThread& rt) {
	bool collection_reloaded = false;
	{
		// mainthread might be waiting for this thread (Message.getAnswer()) and be holding a readlock
		// detect these cases by only trying for a short time
		// Maybe we can do this better, but that is difficult as we pass CollectionPos
		// pointers across thread boundaries and need to make sure they are valid
		boost::unique_lock<DbAlbumCollection> lock(*(rt.appInstance->albumCollection), boost::defer_lock);
		// There is a race condition here, so use this only as a guess
		int waitTime = rt.messageQueue.size() ? 10 : 100;
		if (lock.try_lock_for(boost::chrono::milliseconds(waitTime))) {
			auto reloadWorker = rt.appInstance->reloadWorker.synchronize();
			rt.appInstance->albumCollection->onCollectionReload(**reloadWorker);
			CollectionPos newTargetPos = rt.appInstance->albumCollection->getTargetPos();
			rt.displayPos.hardSetCenteredPos(newTargetPos);
			reloadWorker->reset();
			collection_reloaded = true;
		} else {
			// looks like a deadlock, retry at the end of the messageQueue
			rt.send<CollectionReloadedMessage>();
		}
	}
	if (collection_reloaded) {
		rt.texCache.onCollectionReload();
	}
	rt.appInstance->redrawMainWin();
}

void RenderThread::threadProc(){
	TRACK_CALL_TEXT("Chronflow RenderThread");
	// Required that we can compile CoverPos Scripts
	CoInitializeScope com_enable{};
	for (;;){
		// TODO: Improve this loop � separate this into startup and normal processing
		if (messageQueue.size() == 0 && doPaint){
			texCache.uploadTextures();
			texCache.trimCache();
			onPaint();
			continue;
		}
		std::unique_ptr<Message> msg = messageQueue.pop();

		if (auto m = dynamic_cast<const StopThreadMessage*>(msg.get())){
			break;
		} else if (auto m = dynamic_cast<PaintMessage*>(msg.get())){
			// do nothing, this is just here so that onPaint may run
		} else if (auto m = dynamic_cast<InitDoneMessage*>(msg.get())) {
			glfwMakeContextCurrent(appInstance->glfwWindow);
			renderer.initGlState();
			texCache.init();
			m->promise.set_value(true);
		} else {
			msg->execute(*this);
		}
	}
	glFinish();
}

void RenderThread::sendMessage(unique_ptr<Message>&& msg){
	messageQueue.push(std::move(msg));
}

void RenderThread::onPaint(){
	TRACK_CALL_TEXT("RenderThread::onPaint");
	collection_read_lock lock(appInstance);
	double frameStart = Helpers::getHighresTimer();

	displayPos.update();
	// continue animation if we are not done
	doPaint = displayPos.isMoving();
	//doPaint = true;

	renderer.drawFrame();

	// this might not be right � do we need a glFinish() here?
	double frameEnd = Helpers::getHighresTimer();
	renderer.fpsCounter.recordFrame(frameStart, frameEnd);

	renderer.ensureVSync(cfgVSyncMode != VSYNC_SLEEP_ONLY);
	if (cfgVSyncMode == VSYNC_AND_SLEEP || cfgVSyncMode == VSYNC_SLEEP_ONLY){
		double currentTime = Helpers::getHighresTimer();
										 // time we have        time we already have spend
		int sleepTime = static_cast<int>((1000.0/refreshRate) - 1000*(currentTime - afterLastSwap));
		if (cfgVSyncMode == VSYNC_AND_SLEEP)
			sleepTime -= 2 * timerResolution;
		else
			sleepTime -= timerResolution;
		if (sleepTime >= 0){
			if (!timerInPeriod){
				timeBeginPeriod(timerResolution);
				timerInPeriod = true;
			}
			Sleep(sleepTime);
		}
	}

	renderer.swapBuffers();
	afterLastSwap = Helpers::getHighresTimer();

	if (doPaint){
		this->send<PaintMessage>();
	} else {
		if (timerInPeriod){
			timeEndPeriod(timerResolution);
			timerInPeriod = false;
		}
	}
}



RenderThread::RenderThread(AppInstance* appInstance)
	: appInstance(appInstance),
	  displayPos(appInstance, appInstance->albumCollection->begin()),
	  renderer(appInstance, &displayPos),
	  texCache(appInstance),
	  findAsYouType(appInstance),
	  afterLastSwap(0){
	renderer.texCache = &texCache;

	doPaint = false;

	timerInPeriod = false;
	timerResolution = 10;
	TIMECAPS tc;
	if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR){
		timerResolution = min(max(tc.wPeriodMin, (UINT)1), tc.wPeriodMax);
	}
	
	updateRefreshRate();
	renderThread = (HANDLE)_beginthreadex(0, 0, &(this->runRenderThread), (void*)this, 0, 0);
}

RenderThread::~RenderThread(){
	IF_DEBUG(Console::println(L"Destroying RenderThread"));
	this->send<StopThreadMessage>();
	WaitForSingleObject(renderThread, INFINITE);
	CloseHandle(renderThread);
}


unsigned int WINAPI RenderThread::runRenderThread(void* lpParameter)
{
	reinterpret_cast<RenderThread*>(lpParameter)->threadProc();
	return 0;
}


void RenderThread::updateRefreshRate(){
	DEVMODE dispSettings;
	ZeroMemory(&dispSettings,sizeof(dispSettings));
	dispSettings.dmSize=sizeof(dispSettings);

	if (0 != EnumDisplaySettings(NULL,ENUM_CURRENT_SETTINGS,&dispSettings)){
		refreshRate = dispSettings.dmDisplayFrequency;
		if (refreshRate >= 100) // we do not need 100fps - 50 is enough
			refreshRate /= 2;
	}
}
