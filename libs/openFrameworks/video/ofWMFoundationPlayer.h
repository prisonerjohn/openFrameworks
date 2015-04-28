#pragma once


//ofxWMFVideoPlayer addon written by Philippe Laulheret for Second Story (secondstory.com)
//Based upon Windows SDK samples
//MIT Licensing


#include "ofMain.h"
#include "ofWMFoundationPlayerUtils.h"

#include "EVRPresenter.h"

class CPlayer;

class ofWMFoundationPlayer : public ofBaseVideoPlayer 
{
private:
	static int _instanceCount;

	HWND _hwndPlayer;

	int _width;
	int _height;

	bool _waitingForLoad;
	bool _waitingToPlay;
	bool _waitingToSetPosition;
	bool _waitingToSetVolume;

	bool _isLooping;
	float _currentVolume;
	float _currentPosition;

	ofTexture _sharedTex;
	bool _sharedTextureCreated;

	ofPixels _pixels;

	BOOL InitInstance();
	
	void OnPlayerEvent(HWND hwnd, WPARAM pUnkPtr);

	bool endLoad();
	bool loadEventSent;
	bool bLoaded;
	float _frameRate;

public:
	CPlayer* _player;

	int _id;

	ofWMFoundationPlayer();
	~ofWMFoundationPlayer();

	bool loadMovie(string name);
	bool loadMovie(string name, bool asynchronous);
	void close();
	void update();

	void play();
	void stop();		
	void setPaused(bool bPaused);

	bool isBuffering();
    float getBufferDuration();
    float getBufferProgress();

	float getPosition();
	float getDuration();
	float getFrameRate();

	void setPosition(float pos);

	void setVolume(float vol);
	float getVolume();

	float getHeight();
	float getWidth();

	bool isPlaying(); 
	bool isStopped();
	bool isPaused();

	bool isLooping();
	void setLoopState(ofLoopType loopType);
	bool getIsMovieDone();

	bool setSpeed(float speed, bool useThinning = false); //thinning drops delta frames for faster playback though appears to be choppy, default is false
	float getSpeed();

	bool isLoaded();

	void bind();
	void unbind();
	ofTexture * getTexture();

	unsigned char * getPixels();
	ofPixels& getPixelsRef();
	bool setPixelFormat(ofPixelFormat pixelFormat);
	ofPixelFormat getPixelFormat();

	bool isFrameNew();

	void draw(int x, int y);
	void draw(int x, int y , int w, int h);	

	HWND getHandle() { return _hwndPlayer;}
	LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	static void forceExit();
};