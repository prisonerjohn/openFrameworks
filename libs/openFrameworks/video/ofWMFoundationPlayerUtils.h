//ofxWMFVideoPlayer addon written by Philippe Laulheret for Second Story (secondstory.com)
//Based upon Windows SDK samples
//MIT Licensing


// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

#ifndef PLAYER_H
#define PLAYER_H

#include "ofMain.h"

#include <new>
#include <windows.h>
#include <shobjidl.h> 
#include <shlwapi.h>
#include <assert.h>
#include <tchar.h>
#include <strsafe.h>

// Media Foundation headers
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <evr.h>

#include "EVRPresenter.h"


template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

const UINT WM_APP_PLAYER_EVENT = WM_APP + 1;   

    // WPARAM = IMFMediaEvent*, WPARAM = MediaEventType

enum PlayerState
{
    Closed = 0,			// No session.
    Ready,				// Session was created, ready to open a file. 
    OpenAsyncPending,	// Session is creating URL resource.
	OpenAsyncComplete,  // Session finished opening URL.
    OpenPending,		// Session is opening a file.
    Started,			// Session is playing a file.
    Paused,				// Session is paused.
    Stopped,			// Session is stopped (ready to play). 
    Closing				// Application has closed the session, but is waiting for MESessionClosed.
};

class CPlayer : public IMFAsyncCallback
{
public:
    static HRESULT CreateInstance(HWND hVideo, HWND hEvent, CPlayer **ppPlayer);

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IMFAsyncCallback methods
    STDMETHODIMP  GetParameters(DWORD*, DWORD*)
    {
        // Implementation of this method is optional.
        return E_NOTIMPL;
    }
    STDMETHODIMP  Invoke(IMFAsyncResult* pAsyncResult);

    // Playback
    HRESULT       OpenURL(const WCHAR *sURL);
	HRESULT		  OpenURLAsync(const WCHAR *sURL);
	HRESULT		  EndOpenURL();

    HRESULT       Play();
    HRESULT       Pause();
    HRESULT       Stop();
    HRESULT       Shutdown();
    HRESULT       HandleEvent(UINT_PTR pUnkPtr);

	HRESULT		  GetBufferDuration(DWORD *pDuration);
    HRESULT		  GetBufferProgress(DWORD *pProgress);
	
	PlayerState   GetState() const { return m_state; }

    BOOL          HasVideo() const { return (m_pVideoDisplay != NULL);  }

	HRESULT		  SetPlaybackRate(BOOL bThin, float rateRequested);
	float		  GetPlaybackRate();
	
	float GetDuration();
	float GetPosition();
	float GetWidth() { return m_width; }
	float GetHeight() { return m_height; }

	HRESULT SetPosition(float pos);

	bool GetLooping() { return m_isLooping; }
	void SetLooping(bool isLooping) { m_isLooping = isLooping; }

	HRESULT SetVolume(float vol);
	float   GetVolume() { return m_currentVolume; }

	float GetFrameRate();


protected:
    
    // Constructor is private. Use static CreateInstance method to instantiate.
    CPlayer(HWND hVideo, HWND hEvent);

    // Destructor is private. Caller should call Release.
    virtual ~CPlayer(); 

    HRESULT Initialize();
    HRESULT CreateSession();
    HRESULT CloseSession();
    HRESULT StartPlayback();

	HRESULT SetMediaInfo( IMFPresentationDescriptor *pPD );

    // Media event handlers
    virtual HRESULT OnTopologyStatus(IMFMediaEvent *pEvent);
    virtual HRESULT OnPresentationEnded(IMFMediaEvent *pEvent);
    virtual HRESULT OnNewPresentation(IMFMediaEvent *pEvent);

    // Override to handle additional session events.
    virtual HRESULT OnSessionEvent(IMFMediaEvent*, MediaEventType) 
    { 
        return S_OK; 
    }

protected:
    long                    m_nRefCount;        // Reference count.

    IMFSequencerSource	   *m_pSequencerSource;
    IMFSourceResolver	   *m_pSourceResolver;
    IMFMediaSource         *m_pSource;
    IMFVideoDisplayControl *m_pVideoDisplay;
	MFSequencerElementId	m_previousTopoID;
    HWND                    m_hwndVideo;        // Video window.
    HWND                    m_hwndEvent;        // App window to receive events.
    PlayerState             m_state;            // Current state of the media session.
    HANDLE                  m_hCloseEvent;      // Event to wait on while closing.
	IMFAudioStreamVolume   *m_pVolumeControl;
	bool					m_isLooping;
	int						m_width;
	int						m_height;
	float					m_currentVolume;

public:
	EVRCustomPresenter *m_pEVRPresenter; // Custom EVR for texture sharing
	IMFMediaSession    *m_pSession;
	
	vector<EVRCustomPresenter*> v_EVRPresenters;  //if you want to load multiple sources in one go
	vector<IMFMediaSource*>     v_sources;        //for doing frame symc... this is experimental

};

#endif PLAYER_H
