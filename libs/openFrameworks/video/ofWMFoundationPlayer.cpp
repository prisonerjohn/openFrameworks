//ofWMFoundationPlayer addon written by Philippe Laulheret for Second Story (secondstory.com)
//MIT Licensing


#include "ofWMFoundationPlayerUtils.h"
#include "ofWMFoundationPlayer.h"



typedef std::pair<HWND, ofWMFoundationPlayer*> PlayerItem;
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


int  ofWMFoundationPlayer::_instanceCount = 0;


ofWMFoundationPlayer::ofWMFoundationPlayer() : _player(NULL)
{

	if (_instanceCount == 0)  {
		if (!ofIsGLProgrammableRenderer()){
			if (wglewIsSupported("WGL_NV_DX_interop")){
				ofLogVerbose("ofWMFoundationPlayer") << "WGL_NV_DX_interop supported";
			}
			else{
				ofLogError("ofWMFoundationPlayer") << "WGL_NV_DX_interop not supported. Upgrade your graphc drivers and try again.";
				return;
			}
		}


		HRESULT hr = MFStartup(MF_VERSION);
		if (!SUCCEEDED(hr))
		{
			ofLog(OF_LOG_ERROR, "ofWMFoundationPlayer: Error while loading MF");
		}
	}

	_id = _instanceCount;
	_instanceCount++;
	this->InitInstance();


	_waitingForLoad = false;
	_waitForLoadedToPlay = false;
	_sharedTextureCreated = false;
	_wantToSetVolume = false;
	_currentVolume = 1.0;
	_frameRate = 0.0f;


}


ofWMFoundationPlayer::~ofWMFoundationPlayer() {
	//forceExit();
	if (_player)
	{
		_player->Shutdown();
		//if (_sharedTextureCreated) _player->m_pEVRPresenter->releaseSharedTexture();
		SafeRelease(&_player);
	}

	cout << "Player " << _id << " Terminated" << endl;
	_instanceCount--;
	if (_instanceCount == 0) {
		MFShutdown();

		cout << "Shutting down MF" << endl;
	}
	int i = 1;
}

void ofWMFoundationPlayer::forceExit()
{
	if (_instanceCount != 0)
	{
		cout << "Shutting down MF some ofWMFoundationPlayer remains" << endl;
		MFShutdown();
	}




}

bool ofWMFoundationPlayer::loadMovie(string name){
	return loadMovie(name, true);
}

bool ofWMFoundationPlayer::loadMovie(string name, bool asynchronous)
{
	if (!_player) {
		ofLogError("ofWMFoundationPlayer") << "Player not created. Can't open the movie.";
		return false;
	}

	HRESULT hr = S_OK;
	if(name.find("http") == string::npos){
		DWORD fileAttr = GetFileAttributesA(ofToDataPath(name).c_str());
		if (fileAttr == INVALID_FILE_ATTRIBUTES) {
			stringstream s;
			s << "The video file '" << name << "'is missing.";
			ofLog(OF_LOG_ERROR, "ofWMFoundationPlayer:" + s.str());
			return false;
		}
		string s = ofToDataPath(name);
		std::wstring w(s.length(), L' ');
		std::copy(s.begin(), s.end(), w.begin());

		if(asynchronous){
			hr = _player->OpenURLAsync(w.c_str());
		}
		else{
			hr = _player->OpenURL(w.c_str());
		}
	}
	else{
		string s = name;
		std::wstring w(s.length(), L' ');
		std::copy(s.begin(), s.end(), w.begin());
		if(asynchronous){
			hr = _player->OpenURLAsync(w.c_str());
		}
		else{
			hr = _player->OpenURL(w.c_str());
		}
	}

	if(asynchronous){
		_waitingForLoad = true;
		return true;
	}
	
	_waitForLoadedToPlay = false;

	return endLoad();

}

bool ofWMFoundationPlayer::endLoad(){

	_frameRate = 0.0; //reset frameRate as the new movie loaded might have a different value than previous one

	if (!_sharedTextureCreated)
	{

		_width = _player->getWidth();
		_height = _player->getHeight();

		_tex.allocate(_width, _height, GL_RGBA, true);

		_player->m_pEVRPresenter->createSharedTexture(_width, _height, _tex.texData.textureID);
		_sharedTextureCreated = true;
	}
	else
	{
		if ((_width != _player->getWidth()) || (_height != _player->getHeight()))
		{

			_player->m_pEVRPresenter->releaseSharedTexture();

			_width = _player->getWidth();
			_height = _player->getHeight();

			_tex.allocate(_width, _height, GL_RGBA, true);
			_player->m_pEVRPresenter->createSharedTexture(_width, _height, _tex.texData.textureID);

		}

	}

	return true;

}

void ofWMFoundationPlayer::draw(int x, int y, int w, int h) {


	_player->m_pEVRPresenter->lockSharedTexture();
	_tex.draw(x, y, w, h);
	_player->m_pEVRPresenter->unlockSharedTexture();



}


bool  ofWMFoundationPlayer::isPlaying() {
	return _player->GetState() == Started;
}
bool  ofWMFoundationPlayer::isStopped() {
	return (_player->GetState() == Stopped || _player->GetState() == Paused);
}

bool  ofWMFoundationPlayer::isPaused()
{
	return _player->GetState() == Paused;
}




void	ofWMFoundationPlayer::close() {
	_player->Shutdown();
	_currentVolume = 1.0;
	_wantToSetVolume = false;

}
void	ofWMFoundationPlayer::update() {
	if (!_player) return;

	if(_waitingForLoad && _player->GetState() == OpenAsyncComplete){
		_player->EndOpenURL();
		endLoad();
		_waitingForLoad = false;
	}

	if ((_waitForLoadedToPlay) && _player->GetState() == Paused)
	{
		_waitForLoadedToPlay = false;
		_player->Play();

	}

	DWORD d;
	_player->GetBufferProgress(&d);

	if ((_wantToSetVolume))
	{
		_player->setVolume(_currentVolume);

	}
	return;
}

bool	ofWMFoundationPlayer::getIsMovieDone()
{
	bool bIsDone = false;
	if (getPosition() >= 0.99f)
		bIsDone = true;

	return bIsDone;
}

bool ofWMFoundationPlayer::isLoaded(){
	if (_player == NULL){ return false; }
	PlayerState ps = _player->GetState();
	return ps == PlayerState::Paused || ps == PlayerState::Stopped || ps == PlayerState::Started;
}

unsigned char * ofWMFoundationPlayer::getPixels(){
	if (_tex.isAllocated()){
		_tex.readToPixels(_pixels);
		return _pixels.getPixels();
	}
	return NULL;
}

bool ofWMFoundationPlayer::setPixelFormat(ofPixelFormat pixelFormat){
	return (pixelFormat == OF_PIXELS_RGB);
}

ofPixelFormat ofWMFoundationPlayer::getPixelFormat(){
	return OF_PIXELS_RGB;
}

bool ofWMFoundationPlayer::isFrameNew(){
	return true;//TODO fix this
}

void ofWMFoundationPlayer::setPaused(bool bPause)
{
	if (bPause){
		pause();
	}
	else{
		play();
	}
}

void ofWMFoundationPlayer::play()
{

	if (!_player) return;
	
	if (_player->GetState() == OpenAsyncPending  ||
		_player->GetState() == OpenAsyncComplete || 
		_player->GetState() == OpenPending)
	{
		_waitForLoadedToPlay = true;
	}
	else{
		_player->Play();
	}
}

void	ofWMFoundationPlayer::stop()
{
	_player->Stop();
}

void	ofWMFoundationPlayer::pause()
{
	_player->Pause();
}

void 	ofWMFoundationPlayer::setLoopState(ofLoopType loopType)
{
	switch (loopType)
	{
	case OF_LOOP_NONE:
		setLoop(false);
		break;
	case OF_LOOP_NORMAL:
		setLoop(true);
		break;
	default:
		ofLogError("ofWMFoundationPlayer::setLoopState LOOP TYPE NOT SUPPORTED") << loopType << endl;
		break;
	}
}

float 			ofWMFoundationPlayer::getPosition()
{
	return (_player->getPosition() / getDuration());
	//this returns it in seconds
	//	return _player->getPosition();
}

float 			ofWMFoundationPlayer::getDuration() {
	return _player->getDuration();
}

void ofWMFoundationPlayer::setPosition(float pos)
{
	_player->setPosition(pos);
}

void ofWMFoundationPlayer::setVolume(float vol)
{
	if ((_player) && (_player->GetState() != OpenPending) && (_player->GetState() != Closing) && (_player->GetState() != Closed))  {
		_player->setVolume(vol);
		_wantToSetVolume = false;
	}
	else {
		_wantToSetVolume = true;
	}
	_currentVolume = vol;

}

float ofWMFoundationPlayer::getVolume()
{
	return _player->getVolume();
}

float ofWMFoundationPlayer::getFrameRate()
{
	if (!_player) return 0.0f;
	if (_frameRate == 0.0f) _frameRate = _player->getFrameRate();
	return _frameRate;
}

float ofWMFoundationPlayer::getSpeed() {
	return _player->GetPlaybackRate();
}

bool ofWMFoundationPlayer::setSpeed(float speed, bool useThinning) {
	//according to MSDN playback must be stopped to change between forward and reverse playback and vice versa
	//but is only required to pause in order to shift between forward rates
	float curRate = getSpeed();
	HRESULT hr = S_OK;
	bool resume = isPlaying();
	if (curRate >= 0 && speed >= 0){
		if (!isPaused())
			_player->Pause();
		hr = _player->SetPlaybackRate(useThinning, speed);
		if (resume)
			_player->Play();
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
	if (hr == S_OK)
		return true;
	else{
		if (hr == MF_E_REVERSE_UNSUPPORTED)
			cout << "The object does not support reverse playback." << endl;
		if (hr == MF_E_THINNING_UNSUPPORTED)
			cout << "The object does not support thinning." << endl;
		if (hr == MF_E_UNSUPPORTED_RATE)
			cout << "The object does not support the requested playback rate." << endl;
		if (hr == MF_E_UNSUPPORTED_RATE_TRANSITION)
			cout << "The object cannot change to the new rate while in the running state." << endl;
		return false;
	}
}

float	ofWMFoundationPlayer::getHeight() { return _player->getHeight(); }
float	ofWMFoundationPlayer::getWidth() { return _player->getWidth(); }

void  ofWMFoundationPlayer::setLoop(bool isLooping) { _isLooping = isLooping; _player->setLooping(isLooping); }



//-----------------------------------
// Prvate Functions
//-----------------------------------



// Handler for Media Session events.
void ofWMFoundationPlayer::OnPlayerEvent(HWND hwnd, WPARAM pUnkPtr)
{
	HRESULT hr = _player->HandleEvent(pUnkPtr);
	if (FAILED(hr))
	{
		ofLogError("ofWMFoundationPlayer", "An error occurred. %x", hr);
	}
}



LRESULT CALLBACK WndProcDummy(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{


	switch (message)
	{

	case WM_CREATE:
	{
		return DefWindowProc(hwnd, message, wParam, lParam);

	}
	default:
	{
		ofWMFoundationPlayer*   myPlayer = findPlayers(hwnd);
		if (!myPlayer) return DefWindowProc(hwnd, message, wParam, lParam);
		return myPlayer->WndProc(hwnd, message, wParam, lParam);
	}
	}
	return 0;
}



LRESULT  ofWMFoundationPlayer::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{

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
	PCWSTR szWindowClass = L"MFBASICPLAYBACK";
	HWND hwnd;
	WNDCLASSEX wcex;

	//   g_hInstance = hInst; // Store the instance handle.

	// Register the window class.
	ZeroMemory(&wcex, sizeof(WNDCLASSEX));
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;

	wcex.lpfnWndProc = WndProcDummy;
	//  wcex.hInstance      = hInst;
	wcex.hbrBackground = (HBRUSH)(BLACK_BRUSH);
	// wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_MFPLAYBACK);
	wcex.lpszClassName = szWindowClass;

	if (RegisterClassEx(&wcex) == 0)
	{
		// return FALSE;
	}


	// Create the application window.
	hwnd = CreateWindow(szWindowClass, L"", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, NULL, NULL);

	if (hwnd == 0)
	{
		return FALSE;
	}


	g_WMFVideoPlayers.push_back(std::pair<HWND, ofWMFoundationPlayer*>(hwnd, this));
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



