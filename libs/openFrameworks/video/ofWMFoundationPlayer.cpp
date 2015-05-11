//ofWMFoundationPlayer addon written by Philippe Laulheret for Second Story (secondstory.com)
//MIT Licensing


#include "ofWMFoundationPlayerUtils.h"
#include "ofWMFoundationPlayer.h"

typedef std::pair<HWND,ofWMFoundationPlayer*> PlayerItem;
list<PlayerItem> g_WMFVideoPlayers;

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
// Message handlers

ofWMFoundationPlayer* findPlayers(HWND hwnd)
{
	for (PlayerItem e : g_WMFVideoPlayers)
	{
		if (e.first == hwnd) return e.second;
	}

	return NULL;
}

int ofWMFoundationPlayer::_instanceCount = 0;

ofWMFoundationPlayer::ofWMFoundationPlayer() 
	: _player(NULL)
{
	if (_instanceCount ==0) {
		if (!ofIsGLProgrammableRenderer()) {
			if (wglewIsSupported("WGL_NV_DX_interop")) {
				ofLogVerbose("ofWMFoundationPlayer::ofWMFoundationPlayer") << "WGL_NV_DX_interop supported";
			}
			else {
				ofLogError("ofWMFoundationPlayer:ofWMFoundationPlayer") << "WGL_NV_DX_interop not supported. Upgrade your graphc drivers and try again.";
				return;
			}
		}

		HRESULT hr = MFStartup(MF_VERSION);
		if (!SUCCEEDED(hr)) {
			ofLogError("ofWMFoundationPlayer:ofWMFoundationPlayer") << "Error starting up MediaFoundation";
		}
	}

	_id = _instanceCount;
	_instanceCount++;
	this->InitInstance();

	_waitingForLoad = false;
	_waitingToPlay = false;
	_waitingToSetPosition = false;
	_waitingToSetVolume = false;

	_sharedTextureCreated = false;
	_currentVolume = 1.0;
	_frameRate = 0.0f;
}
	 
ofWMFoundationPlayer::~ofWMFoundationPlayer() 
{
	if (_player) {
		if (_sharedTextureCreated) {
			_player->m_pEVRPresenter->releaseSharedTexture();
		}
		_player->Shutdown();
        SafeRelease(&_player);
    }

	ofLogVerbose("ofWMFoundationPlayer::~ofWMFoundationPlayer") << "Player " << _id << " terminated";

	_instanceCount--;
	if (_instanceCount == 0) {
		 ofLogVerbose("ofWMFoundationPlayer::~ofWMFoundationPlayer") << "Shutting down MediaFoundation";
		 MFShutdown();
	}
}

void ofWMFoundationPlayer::forceExit()
{
	if (_instanceCount != 0) {
		ofLogWarning("ofWMFoundationPlayer::forceExit") << "Shutting down MediaFoundation but " << _instanceCount << " players remain";
		MFShutdown();
	}
}

bool ofWMFoundationPlayer::loadMovie(string name) 
{
	return loadMovie(name, true);
}

bool ofWMFoundationPlayer::loadMovie(string name, bool asynchronous)
{
	_waitingForLoad = false;
	_waitingToPlay = false;
	_waitingToSetPosition = false;
	_waitingToSetVolume = false;

	_currentVolume = 1.0;
	_frameRate = 0.0f;

	loadEventSent = false;
	bLoaded = false;

	if (!_player) { 
		ofLogError("ofWMFoundationPlayer::loadMovie") << "Player not created. Can't open the movie.";
		return false;
	}

	ofLogVerbose("ofWMFoundationPlayer::loadMovie") << "Videoplayer[" << _id << "] loading " << name << endl;

	HRESULT hr = S_OK;
	string str;
	if (name.find("http") == string::npos) {
		// Loading local file.
		str = ofToDataPath(name);
		if (!ofFile::doesFileExist(str)) {
			ofLogError("ofWMFoundationPlayer::loadMovie") << "The video file " << name << " is missing.";
			return false;
		}
	}
	else {
		// Loading remote file.
		str = name;
	}

	_waitingForLoad = true;

	wstring wstr(str.length(), L' ');
	std::copy(str.begin(), str.end(), wstr.begin());

	if (asynchronous) {
		hr = _player->OpenURLAsync(wstr.c_str());
		return true;
	}

	hr = _player->OpenURL(wstr.c_str());
	return endLoad();
}

bool ofWMFoundationPlayer::endLoad()
{
	if (_sharedTextureCreated && (_width != _player->GetWidth() || _height != _player->GetHeight())) {
		_player->m_pEVRPresenter->releaseSharedTexture();
		_sharedTextureCreated = false;
	}
	
	if (!_sharedTextureCreated) {
		_width = _player->GetWidth();
		_height = _player->GetHeight();

		if (_width > 0 && _height > 0) {
			_sharedTex.allocate(_width, _height, GL_RGB, true);
			_player->m_pEVRPresenter->createSharedTexture(_width, _height, _sharedTex.texData.textureID);
		
			_sharedTextureCreated = true;
		}
	}

	_waitingForLoad = false;

	return true;
}

void ofWMFoundationPlayer::update() 
{
	if (!_player) return;

	// Finish an asynchronous load
	if (_waitingForLoad && _player->GetState() == OpenAsyncComplete) {
		_player->EndOpenURL();
		endLoad();
		_waitingForLoad = false;
	}

	if (_waitingToPlay && _player->GetState() == Paused) {
		_player->Play();
		_waitingToPlay = false;
	}

	if (_waitingToSetVolume && _player->GetState() == Started) {
		_player->SetVolume(_currentVolume);
		_waitingToSetVolume = false;
	}

	if (_waitingToSetPosition && (_player->GetState() == Started || _player->GetState() == Paused)) {
		_player->SetPosition(_currentPosition);
		_waitingToSetPosition = false;
	}
}

void ofWMFoundationPlayer::draw(int x, int y) 
{ 
	draw(x, y, getWidth(), getHeight());
}

void ofWMFoundationPlayer::draw(int x, int y , int w, int h) 
{
	if (!_sharedTextureCreated) return;

	_player->m_pEVRPresenter->lockSharedTexture();
	_sharedTex.draw(x, y, w, h);
	_player->m_pEVRPresenter->unlockSharedTexture();
}

bool ofWMFoundationPlayer::isPlaying() 
{
	return _player->GetState() == Started;
}

bool ofWMFoundationPlayer::isStopped() 
{
	return (_player->GetState() == Stopped || _player->GetState() == Paused);
}

bool ofWMFoundationPlayer::isPaused() 
{
	return _player->GetState() == Paused;
}

bool ofWMFoundationPlayer::isBuffering()
{
	return _player && _player->GetBuffering();
}

float ofWMFoundationPlayer::getBufferDuration()
{
	if (_player == NULL) return 0;

	return getBufferProgress() * getDuration();
}

float ofWMFoundationPlayer::getBufferProgress()
{
	if (_player == NULL) return 0;

	DWORD val;
    _player->GetBufferProgress(&val);
	return val / 100.0f; // 0-1
}

void ofWMFoundationPlayer::close() 
{
	_player->Shutdown();
	_currentVolume = 1.0;
}

bool ofWMFoundationPlayer::isLoaded()
{
	if (_player == NULL) { 
		return false; 
	}

	PlayerState ps = _player->GetState();
	return (ps == PlayerState::Paused || ps == PlayerState::Stopped || ps == PlayerState::Started);
}

void ofWMFoundationPlayer::play() 
{
	if (!_player) return;

	if (_player->GetState() == OpenAsyncPending  ||
		_player->GetState() == OpenAsyncComplete || 
		_player->GetState() == OpenPending) {
		_waitingToPlay = true;
	}
	else {
		_player->Play();
		_waitingToPlay = false;
	}
}

void ofWMFoundationPlayer::stop() 
{
	_player->Stop();
}

void ofWMFoundationPlayer::setPaused(bool bPaused)
{
	if (bPaused){
		_player->Pause();
	}
	else{
		play();
	}
}

bool ofWMFoundationPlayer::isFrameNew()
{
	//TODO fix this
	return (_player->GetState() == PlayerState::Started);
}

float ofWMFoundationPlayer::getPosition() 
{
	return _player->GetPosition() / _player->GetDuration();
}

float ofWMFoundationPlayer::getDuration() 
{
	return _player->GetDuration();
}

void ofWMFoundationPlayer::setPosition(float pos)
{
	if (_player && 
		_player->GetState() != OpenPending && 
		_player->GetState() != Closing && 
		_player->GetState() != Closed)  {
		_player->SetPosition(pos);
		_waitingToSetPosition = false;
	}
	else {
		_waitingToSetPosition = true;
	}

	_currentPosition = pos;
}

bool ofWMFoundationPlayer::getIsMovieDone()
{
	return (getPosition() >= 0.99f);
}

void ofWMFoundationPlayer::setVolume(float vol)
{
	if (_player && 
		_player->GetState() != OpenPending && 
		_player->GetState() != Closing && 
		_player->GetState() != Closed)  {
		_player->SetVolume(vol);
		_waitingToSetVolume = false;
	}
	else {
		_waitingToSetVolume = true;
	}

	_currentVolume = vol;
}

float ofWMFoundationPlayer::getVolume()
{
	return _player->GetVolume();
}

float ofWMFoundationPlayer::getFrameRate()
{
	if (!_player) return 0.0f;

	if (_frameRate == 0.0f) {
		_frameRate = _player->GetFrameRate();
	}

	return _frameRate;
}

float ofWMFoundationPlayer::getSpeed() 
{
	return _player->GetPlaybackRate();
}

bool ofWMFoundationPlayer::setSpeed(float speed, bool useThinning)
{
	//according to MSDN playback must be stopped to change between forward and reverse playback and vice versa
	//but is only required to pause in order to shift between forward rates
	float curRate = getSpeed();
	HRESULT hr = S_OK;
	bool resume = isPlaying();
	if (curRate >= 0 && speed >= 0) {
		if (!isPaused()) {
			_player->Pause();
		}
		hr = _player->SetPlaybackRate(useThinning, speed);
		if (resume) {
			_player->Play();
		}
	}
	else {
		//setting to a negative doesn't seem to work though no error is thrown...
		/*float position = getPosition();
		if(isPlaying())
		_player->Stop();
		hr = _player->SetPlaybackRate(useThinning, speed);
		if(resume){
		_player->Play();
		_player->setPosition(position);
		}*/
	}
	if (hr == S_OK) {
		return true;
	}
	else {
		if (hr == MF_E_REVERSE_UNSUPPORTED) {
			ofLogError("ofWMFoundationPlayer::setSpeed") << "The object does not support reverse playback.";
		}
		else if (hr == MF_E_THINNING_UNSUPPORTED) {
			ofLogError("ofWMFoundationPlayer::setSpeed") << "The object does not support thinning.";
		}
		else if (hr == MF_E_UNSUPPORTED_RATE) {
			ofLogError("ofWMFoundationPlayer::setSpeed") << "The object does not support the requested playback rate.";
		}
		else if (hr == MF_E_UNSUPPORTED_RATE_TRANSITION) {
			ofLogError("ofWMFoundationPlayer::setSpeed") << "The object cannot change to the new rate while in the running state.";
		}
		return false;
	}
}

void ofWMFoundationPlayer::bind()
{ 
	if (_sharedTextureCreated) {
		_player->m_pEVRPresenter->lockSharedTexture();
	}
}

void ofWMFoundationPlayer::unbind()
{ 
	if (_sharedTextureCreated) {
		_player->m_pEVRPresenter->unlockSharedTexture();
	}
}

ofTexture * ofWMFoundationPlayer::getTexture()
{ 
	return &_sharedTex;
}

unsigned char * ofWMFoundationPlayer::getPixels()
{
	if (_sharedTex.isAllocated()) {
		_sharedTex.readToPixels(_pixels);
		return _pixels.getPixels();
	}
	return NULL;
}

ofPixels& ofWMFoundationPlayer::getPixelsRef()
{
	return _pixels;
}

bool ofWMFoundationPlayer::setPixelFormat(ofPixelFormat pixelFormat)
{
	return (pixelFormat == OF_PIXELS_RGB);
}

ofPixelFormat ofWMFoundationPlayer::getPixelFormat()
{
	return OF_PIXELS_RGB;
}

float ofWMFoundationPlayer::getHeight() 
{ 
	return _player->GetHeight(); 
}

float ofWMFoundationPlayer::getWidth() 
{ 
	return _player->GetWidth(); 
}

bool ofWMFoundationPlayer::isLooping() 
{ 
	return _isLooping; 
}

void ofWMFoundationPlayer::setLoopState(ofLoopType loopType)
{
	switch (loopType) {
	case OF_LOOP_NONE:
		_isLooping = false; 
		_player->SetLooping(_isLooping); 
		break;

	case OF_LOOP_NORMAL:
		_isLooping = true; 
		_player->SetLooping(_isLooping);
		break;
	
	default:
		ofLogError("ofWMFoundationPlayer::setLoopState") << "Loop type " << loopType << " not supported";
		break;
	}
}

//-----------------------------------
// Private Functions
//-----------------------------------

// Handler for Media Session events.
void ofWMFoundationPlayer::OnPlayerEvent(HWND hwnd, WPARAM pUnkPtr)
{
	HRESULT hr = _player->HandleEvent(pUnkPtr);
	if (FAILED(hr))
	{
		ofLogError("ofWMFoundationPlayer::OnPlayerEvent") << "An error occurred:" << hr;
	}
}

LRESULT CALLBACK WndProcDummy(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_CREATE:
		return DefWindowProc(hwnd, message, wParam, lParam);
		break;

	default:
		ofWMFoundationPlayer* myPlayer = findPlayers(hwnd);
		if (!myPlayer) {
			return DefWindowProc(hwnd, message, wParam, lParam);
		}
		return myPlayer->WndProc (hwnd, message, wParam, lParam);
		break;
	}

	return 0;
}

LRESULT ofWMFoundationPlayer::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	case WM_APP_PLAYER_EVENT:
		OnPlayerEvent(hwnd, wParam);
		break;

	default:
		return DefWindowProc(hwnd, message, wParam, lParam);
	}

	return 0;
}

//  Create the application window.
BOOL ofWMFoundationPlayer::InitInstance()
{
	PCWSTR szWindowClass = L"MFBASICPLAYBACK" ;
	HWND hwnd;
	WNDCLASSEX wcex;

	//g_hInstance = hInst; // Store the instance handle.

	// Register the window class.
	ZeroMemory(&wcex, sizeof(WNDCLASSEX));
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW ;

	wcex.lpfnWndProc = WndProcDummy;
	//  wcex.hInstance      = hInst;
	wcex.hbrBackground = (HBRUSH)(BLACK_BRUSH);
	// wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_MFPLAYBACK);
	wcex.lpszClassName = szWindowClass;

	if (RegisterClassEx(&wcex) == 0) {
		// return FALSE;
	}

	// Create the application window.
	hwnd = CreateWindow(szWindowClass, L"", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, NULL, NULL);

	if (hwnd == 0) {
		return FALSE;
	}

	g_WMFVideoPlayers.push_back(std::pair<HWND,ofWMFoundationPlayer*>(hwnd,this));
	HRESULT hr = CPlayer::CreateInstance(hwnd, hwnd, &_player); 

	LONG style2 = ::GetWindowLong(hwnd, GWL_STYLE);  
	style2 &= ~WS_DLGFRAME;
	style2 &= ~WS_CAPTION; 
	style2 &= ~WS_BORDER; 
	style2 &= WS_POPUP;
	LONG exstyle2 = ::GetWindowLong(hwnd, GWL_EXSTYLE);  
	exstyle2 &= ~WS_EX_DLGMODALFRAME;  
	::SetWindowLong(hwnd, GWL_STYLE, style2);  
	::SetWindowLong(hwnd, GWL_EXSTYLE, exstyle2);  

	_hwndPlayer = hwnd;

	UpdateWindow(hwnd);

	return TRUE;
}
