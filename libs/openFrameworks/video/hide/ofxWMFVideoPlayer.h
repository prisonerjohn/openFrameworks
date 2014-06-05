#pragma once


//ofxWMFVideoPlayer addon written by Philippe Laulheret for Second Story (secondstory.com)
//Based upon Windows SDK samples
//MIT Licensing


#include "ofMain.h"
#include "ofxWMFVideoPlayerUtils.h"

//#include "EVRPresenter.h"



class ofxWMFVideoPlayer;


class CPlayer;
class ofxWMFVideoPlayer: public ofBaseVideoPlayer{

	private:
		static int  _instanceCount;
		
        bool hasNVidiaExtensions;
		
		HWND		_hwndPlayer;
		
		BOOL bRepaintClient;
		
		
		int _width;
		int _height;


		bool _waitForLoadedToPlay;
		bool _isLooping;

		bool _sharedTextureCreated;
		
		ofTexture _tex;
	
		BOOL InitInstance();
		ofPixels pix;

		
		void                OnPlayerEvent(HWND hwnd, WPARAM pUnkPtr);


	public:
	CPlayer*	_player;

	int _id;

	
	ofxWMFVideoPlayer();
	 ~ofxWMFVideoPlayer();

	 bool				loadMovie(string name);
	 bool 				loadMovie(string name_left, string name_right) ;
	 void				close();
	 void				update();
	
	 void				play();
	 void				stop();		
	 void				pause();

	 float				getPosition();
	 float				getDuration();

	 void				setPosition(float pos);

	 float				getHeight();
	 float				getWidth();

	 bool				isPlaying(); 
	 bool				isStopped();
	 bool				isPaused();

	 void				setLoop(bool isLooping);
	 bool				isLooping() { return _isLooping; }
	 
	 bool				isFrameNew(){return false;};
	 unsigned char * 	getPixels(){return NULL;};
	 ofTexture *		getTexture(){return NULL;};
	 bool				isLoaded(){return false;};
	 bool				setPixelFormat(ofPixelFormat pixelFormat){return false;};
	 ofPixelFormat 		getPixelFormat(){return OF_PIXELS_UNKNOWN;};
	 ofPixelsRef		getPixelsRef(){return pix;};

	 //getSpeed
	 //getIsMovieDone

	 void 				setPaused(bool bPause){};
//	 void 				setPosition(float pct){};
	 void 				setVolume(float volume){}; // 0..1
	 void 				setLoopState(ofLoopType state){};
	 void   			setSpeed(float speed){};
	 void				setFrame(int frame){}; 
	//
	 int				getCurrentFrame(){return 0;};
	 int				getTotalNumFrames(){return 0;};
	 ofLoopType			getLoopState(){return OF_LOOP_NONE;};
	
	 void				firstFrame(){};
	 void				nextFrame(){};
	 void				previousFrame(){};


	void draw(int x, int y , int w, int h);
	void draw(int x, int y) { draw(x,y,getWidth(),getHeight()); }


	HWND getHandle() { return _hwndPlayer;}
	LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	static void forceExit();


};