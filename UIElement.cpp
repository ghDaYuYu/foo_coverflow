#include "stdafx.h"
#include <set>
#include "base.h"
#include "config.h"

#include "AppInstance.h"
#include "AsynchTexLoader.h"
#include "Console.h"
#include "DbAlbumCollection.h"
#include "DisplayPosition.h"
#include "MouseFlicker.h"
#include "MyActions.h"
#include "PlaybackTracer.h"
#include "RenderThread.h"
#include "ScriptedCoverPositions.h"
#include "TrackDropSource.h"

#define MAINWINDOW_CLASSNAME L"foo_chronflow MainWindow"

#define MINIMIZE_CHECK_TIMEOUT 10000 // milliseconds

class FindAsYouType {
	static const int typeTimeout = 1000; // milliseconds
	pfc::string8 enteredString;
	AppInstance* appInstance;
	bool playbackTracerLocked;
public:
	FindAsYouType(AppInstance* instance)
		: appInstance(instance){
		clearSearch();
		playbackTracerLocked = false;
	}
	void enterChar(wchar_t c){
		pfc::string8 newString(enteredString);
		newString << pfc::stringcvt::string_utf8_from_wide(&c, 1);
		if (doSearch(newString)){
			enteredString = newString;
		} else {
			MessageBeep(-1);
		}
		lockPlaybackTracer();
		SetTimer(appInstance->mainWindow, IDT_FIND_AS_YOU_TYPE_RESET, typeTimeout, NULL);
	}
	void removeChar(){
		enteredString.truncate(enteredString.length() - 1);
		if (enteredString.length() == 0){
			unlockPlaybackTracer();
		} else {
			doSearch(enteredString);
			lockPlaybackTracer();
			SetTimer(appInstance->mainWindow, IDT_FIND_AS_YOU_TYPE_RESET, typeTimeout, NULL);
		}
	}
	void clear(){
		unlockPlaybackTracer();
		KillTimer(appInstance->mainWindow, IDT_FIND_AS_YOU_TYPE_RESET);
		clearSearch();
	}

private:
	void lockPlaybackTracer(){
		if (!playbackTracerLocked){
			playbackTracerLocked = true;
			appInstance->playbackTracer->lock();
		}
	}
	void unlockPlaybackTracer(){
		if (playbackTracerLocked){
			playbackTracerLocked = false;
			appInstance->playbackTracer->unlock();
		}
	}
	bool doSearch(const char* searchFor){
		//console::print(pfc::string_formatter() << "searching for: " << searchFor);
		CollectionPos pos;
		if (appInstance->albumCollection->performFayt(searchFor, pos)){
			appInstance->displayPos->setTarget(pos);
			return true;
		} else {
			return false;
		}
	}
	void clearSearch(){
		enteredString.reset();
	}
};

class Chronflow : public ui_element_instance {
public:
	static GUID g_get_guid() {
		// {1D56881C-CA24-470c-944A-DED830F9E95D}
		static const GUID guid_foo_chronflow = { 0x1d56881c, 0xca24, 0x470c, { 0x94, 0x4a, 0xde, 0xd8, 0x30, 0xf9, 0xe9, 0x5d } };
		return guid_foo_chronflow;
	}
	static GUID g_get_subclass() { return ui_element_subclass_media_library_viewers; }

	GUID get_guid() { return Chronflow::g_get_guid(); }
	GUID get_subclass() { return Chronflow::g_get_subclass(); }

	static void g_get_name(pfc::string_base & out) { out = "Chronflow"; }
	static const char * g_get_description() { return "Displays a 3D rendering of the Album Art in your Media Library"; }

	static ui_element_config::ptr g_get_default_configuration() { return ui_element_config::g_create_empty(g_get_guid()); }

	HWND get_wnd() {
		return appInstance->mainWindow;
	};

private:
	ULONG_PTR gdiplusToken;
	MouseFlicker* mouseFlicker;
	FindAsYouType* findAsYouType;

	bool mainWinMinimized;
	ui_element_config::ptr config;
	const ui_element_instance_callback_ptr callback;
public:
	AppInstance* appInstance;
	static std::set<Chronflow*> instances;

public:
	Chronflow(ui_element_config::ptr config, ui_element_instance_callback_ptr p_callback) : callback(p_callback), config(config) {
		mouseFlicker = 0;
		findAsYouType = 0;
		mainWinMinimized = true;
		appInstance = new AppInstance();;
		#ifdef _DEBUG
		Console::create();
		#endif
		instances.insert(this);
	}
	~Chronflow(){
		IF_DEBUG(Console::println(L"Destroying UiElement"));
		appInstance->renderer->stopRenderThread();
		appInstance->texLoader->stopWorkerThread();
		instances.erase(this);
		delete pfc::replace_null_t(mouseFlicker);
		delete pfc::replace_null_t(findAsYouType);

		delete pfc::replace_null_t(appInstance->texLoader);
		delete pfc::replace_null_t(appInstance->renderer);
		delete pfc::replace_null_t(appInstance->displayPos);
		delete pfc::replace_null_t(appInstance->albumCollection);
		delete pfc::replace_null_t(appInstance->playbackTracer);

		if (appInstance->mainWindow)
			DestroyWindow(appInstance->mainWindow); // This also deletes the LoaderWindow!
		appInstance->mainWindow = 0;

		delete pfc::replace_null_t(appInstance);

		Gdiplus::GdiplusShutdown(gdiplusToken);
	}

	void initialize_window(HWND parent) {
		Gdiplus::GdiplusStartupInput gdiplusStartupInput;
		// TODO: catch errors from this call
		Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

		appInstance->mainWindow = createWindow(parent);
		appInstance->renderer = new RenderThread(appInstance);
		auto attachMessage = make_shared<RTAttachMessage>();
		appInstance->renderer->send(attachMessage);
		if (!attachMessage->getAnswer()){
			throw std::exception("Renderer failed to attach to window");
		}
		if (cfgMultisampling){
			auto multiSamplingMessage = make_shared<RTMultiSamplingMessage>();
			appInstance->renderer->send(multiSamplingMessage);
			if (multiSamplingMessage->getAnswer()){
				DestroyWindow(appInstance->mainWindow);
				appInstance->mainWindow = createWindow(parent);
				auto attachMessage = make_shared<RTAttachMessage>();
				appInstance->renderer->send(attachMessage);
				if (!attachMessage->getAnswer()){
					throw std::exception("Renderer failed to attach to window for Multisampling");
				}
			}
		}

		appInstance->albumCollection = new DbAlbumCollection(appInstance);
		appInstance->texLoader = new AsynchTexLoader(appInstance);
		appInstance->displayPos = new DisplayPosition(appInstance, appInstance->albumCollection->begin());
		appInstance->playbackTracer = new PlaybackTracer(appInstance);
		findAsYouType = new FindAsYouType(appInstance);
		mouseFlicker = new MouseFlicker(appInstance);
		
		appInstance->albumCollection->reloadAsynchStart();
	}


	void set_configuration(ui_element_config::ptr config) { this->config = config; }
	ui_element_config::ptr get_configuration() { return config; }

	static bool registerWindowClass(){
		HINSTANCE myInstance = core_api::get_my_instance();

		WNDCLASS wc = { 0 };
		wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS | CS_NOCLOSE;
		wc.lpfnWndProc = (WNDPROC)Chronflow::WndProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = sizeof(Chronflow *);
		wc.hInstance = myInstance;
		wc.hIcon = LoadIcon(NULL, IDI_HAND);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = NULL;
		wc.lpszMenuName = NULL;
		wc.lpszClassName = MAINWINDOW_CLASSNAME;

		return RegisterClass(&wc) != 0;
	}

private:
	LRESULT MessageHandler (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
		switch (uMsg){
			case WM_COLLECTION_REFRESHED:
			{
				std::unique_lock<AppInstance> lock(*appInstance);
				appInstance->albumCollection->reloadAsynchFinish(lParam);
				return 0;
			}
			case WM_DESTROY:
				if (appInstance->renderer){
					auto msg = make_shared<RTUnattachMessage>();
					appInstance->renderer->send(msg);
					msg->getAnswer();
				}
				return 0;
			case WM_NCDESTROY:
				appInstance->mainWindow = 0;
				return 0;
			case WM_SIZE:
				if (appInstance->renderer){
					auto msg = make_shared<RTWindowResizeMessage>(LOWORD(lParam), HIWORD(lParam));
					appInstance->renderer->send(msg);
				}
				return 0;
			case WM_DISPLAYCHANGE:
			case WM_DEVMODECHANGE:
			{
				if (appInstance->renderer)
					appInstance->renderer->send(make_shared<RTDeviceModeMessage>());
				return 0;
			}
			case WM_MOUSEWHEEL:
			{
				static int zDelta = 0;
				zDelta -= GET_WHEEL_DELTA_WPARAM(wParam);
				int m = zDelta / WHEEL_DELTA;
				zDelta = zDelta % WHEEL_DELTA;
				appInstance->displayPos->moveTargetBy(m);
				appInstance->playbackTracer->userStartedMovement();
				return 0;
			}
			case WM_MBUTTONDOWN:
			{
				auto msg = make_shared<RTGetOffsetMessage>(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
				appInstance->renderer->send(msg);
				auto answer = msg->getAnswer();
				if (answer.first){
					CollectionPos newTarget = appInstance->displayPos->getOffsetPos(answer.second);
					appInstance->displayPos->setTarget(newTarget);
					appInstance->playbackTracer->userStartedMovement();
					executeAction(cfgMiddleClick, newTarget);
				}
				return 0;
			}
			case WM_LBUTTONDBLCLK:
			case WM_LBUTTONDOWN:
			{
				SetFocus(appInstance->mainWindow);
				auto msg = make_shared<RTGetOffsetMessage>(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
				appInstance->renderer->send(msg);
				auto answer = msg->getAnswer();
				if (answer.first){
					CollectionPos clickedCover = appInstance->displayPos->getOffsetPos(answer.second);

					POINT pt;
					GetCursorPos(&pt);
					if (DragDetect(hWnd, pt)) {
						metadb_handle_list tracks;
						appInstance->albumCollection->getTracks(clickedCover, tracks);

						// Create an IDataObject that contains the dragged track.
						static_api_ptr_t<playlist_incoming_item_filter> piif;
						// create_dataobject_ex() returns a smart pointer unlike create_dataobject()
						// which returns a raw COM pointer. The less chance we have to accidentally
						// get the reference counting wrong, the better.
						pfc::com_ptr_t<IDataObject> pDataObject = piif->create_dataobject_ex(tracks);

						// Create an IDropSource.
						// The constructor of IDropSource_tutorial1 is hidden by design; we use the
						// provided factory method which returns a smart pointer.
						pfc::com_ptr_t<IDropSource> pDropSource = TrackDropSource::g_create(hWnd);

						DWORD effect;
						// Perform drag&drop operation.
						DoDragDrop(pDataObject.get_ptr(), pDropSource.get_ptr(), DROPEFFECT_COPY, &effect);
					} else {
						appInstance->displayPos->setTarget(clickedCover);
						appInstance->playbackTracer->userStartedMovement();
						if (uMsg == WM_LBUTTONDBLCLK)
							executeAction(cfgDoubleClick, clickedCover);
					}
				} else {
					mouseFlicker->mouseDown(hWnd, GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
				}
				return 0;
			}
			case WM_LBUTTONUP:
				mouseFlicker->mouseUp(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
				return 0;
			case WM_CONTEXTMENU:
				if (callback->is_edit_mode_enabled()){
					return DefWindowProc(hWnd, uMsg, wParam, lParam);
				}
				onContextMenu(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
				return 0;
			case WM_MOUSEMOVE:
				mouseFlicker->mouseMove(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
				return 0;
			case WM_CAPTURECHANGED:
				mouseFlicker->lostCapture(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
				return 0;
			case WM_TIMER:
				switch (wParam){
					case IDT_PLAYBACK_TRACER:
						appInstance->playbackTracer->timerHit();
						break;
					case IDT_FIND_AS_YOU_TYPE_RESET:
						findAsYouType->clear();
						break;
					case IDT_CHECK_MINIMIZED:
						onCheckMinimizeTimerHit();
						break;
				}
				return 0;
			case WM_GETDLGCODE:
				return DLGC_WANTALLKEYS;
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
			{
				if (wParam == VK_RETURN){
					executeAction(cfgEnterKey, appInstance->displayPos->getTarget());
					return 0;
				} else if (wParam == VK_F5){
					appInstance->albumCollection->reloadAsynchStart();
					return 0;
				} else if (wParam == VK_F6){
					appInstance->playbackTracer->moveToNowPlaying();
					return 0;
				} else if (wParam == VK_RIGHT || wParam == VK_LEFT || wParam == VK_NEXT || wParam == VK_PRIOR
						|| wParam == VK_HOME || wParam == VK_END) {
					int move = 0;
					if (wParam == VK_RIGHT){
						move = +1;
					} else if (wParam == VK_LEFT){
						move = -1;
					} else if (wParam == VK_NEXT){
						move = +10;
					} else if (wParam == VK_PRIOR){
						move = -10;
					} else if (wParam == VK_END){
						// TODO: This is a dirty hack
						move = +10000000;
					} else if (wParam == VK_HOME){
						move -= 10000000;
					}
					if (move){
						move *= LOWORD(lParam);
						ScopeCS scopeLock(appInstance->displayPos->accessCS);
						appInstance->displayPos->moveTargetBy(move);
						appInstance->playbackTracer->userStartedMovement();
						return 0;
					}
				} else if (cfgFindAsYouType &&
						   (uMsg == WM_KEYDOWN) &&
						   ((wParam > 'A' && wParam < 'Z') || (wParam > '0' && wParam < '9') || (wParam == ' ')) &&
						   ((GetKeyState(VK_CONTROL) & 0x8000) == 0)){
					// disable hotkeys that interfer with find-as-you-type
					return 0;
				} else {
					static_api_ptr_t<keyboard_shortcut_manager> ksm;
					if (ksm->on_keydown(ksm->TYPE_MAIN,wParam)){
						return 0;
					} else {
						metadb_handle_list tracks;
						if (appInstance->albumCollection->getTracks(appInstance->displayPos->getTarget(), tracks)){
							if (ksm->on_keydown_context(tracks, wParam, contextmenu_item::caller_undefined))
								return 0;
						}
					}
				}
				break;
			}
			case WM_CHAR:
			{
				if (cfgFindAsYouType){
					switch (wParam) 
					{ 
					case 1: // any other nonchar character
					case 0x09: // Process a tab. 
						break;

					case 0x08: // Process a backspace. 
						findAsYouType->removeChar();
						break; 


					case 0x0A: // Process a linefeed. 
					case 0x0D: // Process a carriage return. 
					case 0x1B: // Process an escape. 
						findAsYouType->clear();
						break; 

					default: // Process any writeable character
						findAsYouType->enterChar(wParam);
						break; 
					} 
					return 0;
				}
				break;
			}
			case WM_ERASEBKGND:
				return TRUE;
			case WM_PAINT:
			{
				if (GetUpdateRect(hWnd, 0, FALSE)){
					appInstance->renderer->send(make_shared<RTRedrawMessage>());
					ValidateRect(hWnd,NULL);
					if (cfgEmptyCacheOnMinimize){
						if (mainWinMinimized){
							mainWinMinimized = false;
							appInstance->texLoader->resumeLoading();
						}
						SetTimer(hWnd, IDT_CHECK_MINIMIZED, MINIMIZE_CHECK_TIMEOUT, 0);
					}
				}
				return 0;
			}
		}
		return DefWindowProc(hWnd,uMsg,wParam,lParam);
	}
	static LRESULT CALLBACK WndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
		Chronflow* chronflow = 0;
		if (uMsg == WM_NCCREATE){
			chronflow = reinterpret_cast<Chronflow*>(((CREATESTRUCT*)lParam)->lpCreateParams);
			SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)chronflow);
		} else {
			chronflow = reinterpret_cast<Chronflow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
		}
		if (chronflow == 0)
			return DefWindowProc(hWnd,uMsg,wParam,lParam);
		return chronflow->MessageHandler(hWnd, uMsg, wParam, lParam);
	}
	inline void executeAction(const char * action, CollectionPos forCover){
		metadb_handle_list tracks;
		appInstance->albumCollection->getTracks(forCover, tracks);
		executeAction(action, forCover, tracks);
	}
	void executeAction(const char * action, CollectionPos forCover, const metadb_handle_list& tracks){
		pfc::string8 albumTitle;
		appInstance->albumCollection->getTitle(forCover, albumTitle);
		for (int i=0; i < tabsize(g_customActions); i++){
			if (stricmp_utf8(action, g_customActions[i]->actionName) == 0){
				g_customActions[i]->run(tracks, albumTitle);
				return;
			}
		}
		GUID commandGuid;
		if (menu_helpers::find_command_by_name(action, commandGuid)){
			menu_helpers::run_command_context(commandGuid, pfc::guid_null, tracks);
		}
	}
	void onCheckMinimizeTimerHit(){
		if (cfgEmptyCacheOnMinimize){
			if (appInstance->isMainWinMinimized()){
				if (!mainWinMinimized){
					mainWinMinimized = true;
					appInstance->texLoader->pauseLoading();
					appInstance->texLoader->clearCache();
					KillTimer(appInstance->mainWindow, IDT_CHECK_MINIMIZED);
				}
			}
		} else {
			KillTimer(appInstance->mainWindow, IDT_CHECK_MINIMIZED);
			if (mainWinMinimized){
				mainWinMinimized = false;
				appInstance->texLoader->resumeLoading();
			}
		}
	}
	void onContextMenu (HWND hWnd, const int x, const int y){
		// FIXME handle not yet loaded collection � in ohter interaction handler too
		PlaybackTracerScopeLock lock(appInstance->playbackTracer);
		POINT pt;
		metadb_handle_list tracks;
		CollectionPos target = appInstance->displayPos->getTarget();
		if (x == -1){
			pt.x = 0;
			pt.y = 0;
			ClientToScreen(hWnd, &pt);
		} else {
			pt.x = x;
			pt.y = y;
			POINT clientPt = pt;
			ScreenToClient(hWnd, &clientPt);
			auto msg = make_shared<RTGetOffsetMessage>(pt.x, pt.y);
			appInstance->renderer->send(msg);
			auto answer = msg->getAnswer();
			if (answer.first){
				target = appInstance->displayPos->getOffsetPos(answer.second);
			}
		}
		appInstance->albumCollection->getTracks(target, tracks);

		enum {
			ID_ENTER = 1,
			ID_ENTER2,
			ID_DOUBLECLICK,
			ID_MIDDLECLICK,
			ID_PREFERENCES,
			ID_CONTEXT_FIRST,
			ID_CONTEXT_LAST = ID_CONTEXT_FIRST + 1000,
		};

		HMENU hMenu = CreatePopupMenu();
		if ((cfgEnterKey.length() > 0) && (strcmp(cfgEnterKey, cfgDoubleClick) != 0))
			uAppendMenu(hMenu, MF_STRING, ID_ENTER, pfc::string8(cfgEnterKey) << "\tEnter");
		if (cfgDoubleClick.length() > 0)
			uAppendMenu(hMenu, MF_STRING, ID_DOUBLECLICK, pfc::string8(cfgDoubleClick) << "\tDouble Click");
		if ((cfgMiddleClick.length() > 0) && (strcmp(cfgMiddleClick, cfgDoubleClick) != 0) && (strcmp(cfgMiddleClick, cfgEnterKey) != 0))
			uAppendMenu(hMenu, MF_STRING, ID_MIDDLECLICK, pfc::string8(cfgMiddleClick) << "\tMiddle Click");

		service_ptr_t<contextmenu_manager> cmm;
		contextmenu_manager::g_create(cmm);
		cmm->init_context(tracks, contextmenu_manager::FLAG_SHOW_SHORTCUTS);
		if (cmm->get_root()){
			if (GetMenuItemCount(hMenu) > 0)
				uAppendMenu(hMenu, MF_SEPARATOR, 0, 0);
			cmm->win32_build_menu(hMenu, ID_CONTEXT_FIRST, ID_CONTEXT_LAST);
			uAppendMenu(hMenu, MF_SEPARATOR, 0, 0);
		}
		uAppendMenu(hMenu, MF_STRING, ID_PREFERENCES, "Chronflow Preferences...");

		menu_helpers::win32_auto_mnemonics(hMenu);
		int cmd = TrackPopupMenu(hMenu, TPM_NONOTIFY | TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NOANIMATION, 
			pt.x, pt.y, 0, hWnd, 0);
		DestroyMenu(hMenu);
		if (cmd == ID_PREFERENCES){
			static_api_ptr_t<ui_control>()->show_preferences(guid_configWindow);
		} else if (cmd == ID_ENTER){
			executeAction(cfgEnterKey, target, tracks);
		} else if (cmd == ID_DOUBLECLICK){
			executeAction(cfgDoubleClick, target, tracks);
		} else if (cmd == ID_MIDDLECLICK){
			executeAction(cfgMiddleClick, target, tracks);
		} else if (cmd >= ID_CONTEXT_FIRST && cmd <= ID_CONTEXT_LAST){
			cmm->execute_by_id(cmd - ID_CONTEXT_FIRST);
		}
		//contextmenu_manager::win32_run_menu_context(hWnd, tracks, &pt);
	}
	HWND createWindow(HWND parent){
		HWND		hWnd;

		WIN32_OP(hWnd = CreateWindowEx(
			0,									// Extended Style For The Window
			MAINWINDOW_CLASSNAME,				// Class Name
			L"ChronFlow MainWin",				// Window Title
			WS_CHILD |							// Defined Window Style
			WS_CLIPSIBLINGS |					// Required Window Style
			WS_CLIPCHILDREN,					// Required Window Style*/
			CW_USEDEFAULT, CW_USEDEFAULT,		// Window Position
			CW_USEDEFAULT, CW_USEDEFAULT,		// Window Dimensions
			parent,								// No Parent Window
			NULL,								// No Menu
			core_api::get_my_instance(),		// Instance
			(void*)this));

		return hWnd;
	}
};

std::set<Chronflow*> Chronflow::instances = std::set<Chronflow*>();


class UiElement : public ui_element {
public:
	GUID get_guid() { return Chronflow::g_get_guid(); }
	GUID get_subclass() { return Chronflow::g_get_subclass(); }
	void get_name(pfc::string_base & out) { Chronflow::g_get_name(out); }
	ui_element_instance::ptr instantiate(HWND parent, ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback) {
		PFC_ASSERT(cfg->get_guid() == get_guid());
		service_nnptr_t<Chronflow> item = new service_impl_t<Chronflow>(cfg, callback);
		item->initialize_window(parent);
		return item;
	}
	ui_element_config::ptr get_default_configuration() { return Chronflow::g_get_default_configuration(); }
	ui_element_children_enumerator_ptr enumerate_children(ui_element_config::ptr cfg) { return NULL; }
	bool get_description(pfc::string_base & out) { out = Chronflow::g_get_description(); return true; }
};

static service_factory_single_t<UiElement> uiElement;

void getAppInstances(pfc::array_t<AppInstance*> &instances){
	instances.set_count(0);
	for (Chronflow* i : Chronflow::instances){
		instances.append_single_val(i->appInstance);
	}
}


class InitHandler : public init_stage_callback {
public:
	void on_init_stage(t_uint32 stage){
		if (stage == init_stages::before_ui_init){
			registerWindowClasses();
		}
	}

private:
	void registerWindowClasses(){
		// Note: We do not need to unregister these classes as it happens automatically when foobar quits
		if (!Chronflow::registerWindowClass()){
			errorPopupWin32("Failed to register MainWindow class");
		}

		HINSTANCE myInstance = core_api::get_my_instance();

		WNDCLASS	wc = { 0 };
		wc.style = CS_OWNDC | CS_NOCLOSE;
		wc.lpfnWndProc = DefWindowProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = myInstance;
		wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wc.hCursor = NULL;
		wc.hbrBackground = NULL;
		wc.lpszMenuName = NULL;
		wc.lpszClassName = L"Chronflow LoaderWindow";

		if (!RegisterClass(&wc)){
			errorPopupWin32("Failed to register Loader Window class");
		}
	}
};

static service_factory_single_t<InitHandler> initHandler;