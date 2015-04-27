// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

#include "ofWMFoundationPlayerUtils.h"
#include <assert.h>

#pragma comment(lib, "shlwapi")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "Shlwapi.lib")


#include "Presenter.h"


template <class Q>
HRESULT GetEventObject(IMFMediaEvent *pEvent, Q **ppObject)
{
    *ppObject = NULL;   // zero output

    PROPVARIANT var;
    HRESULT hr = pEvent->GetValue(&var);
    if (SUCCEEDED(hr))
    {
        if (var.vt == VT_UNKNOWN)
        {
            hr = var.punkVal->QueryInterface(ppObject);
        }
        else
        {
            hr = MF_E_INVALIDTYPE;
        }
        PropVariantClear(&var);
    }
    return hr;
}

HRESULT CreatePlaybackTopology(IMFMediaSource *pSource, 
							   IMFPresentationDescriptor *pPD, HWND hVideoWnd,IMFTopology **ppTopology,IMFVideoPresenter *pVideoPresenter);

HRESULT AddToPlaybackTopology(IMFMediaSource *pSource,
							  IMFPresentationDescriptor *pPD, HWND hVideoWnd,IMFTopology *pTopology,IMFVideoPresenter *pVideoPresenter);

//  Static class method to create the CPlayer object.

HRESULT CPlayer::CreateInstance(
    HWND hVideo,                  // Video window.
    HWND hEvent,                  // Window to receive notifications.
    CPlayer **ppPlayer)           // Receives a pointer to the CPlayer object.
{
    if (ppPlayer == NULL)
    {
        return E_POINTER;
    }

    CPlayer *pPlayer = new (std::nothrow) CPlayer(hVideo, hEvent);
    if (pPlayer == NULL)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pPlayer->Initialize();
    if (SUCCEEDED(hr))
    {
        *ppPlayer = pPlayer;
    }
    else
    {
        pPlayer->Release();
    }
    return hr;
}

HRESULT CPlayer::Initialize()
{ 
   HRESULT hr = 0;

   m_hCloseEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
   if (m_hCloseEvent == NULL)
   {
	   hr = HRESULT_FROM_WIN32(GetLastError());
   }

   if (!m_pEVRPresenter)  { 
	   m_pEVRPresenter = new EVRCustomPresenter(hr);
	   m_pEVRPresenter->SetVideoWindow(m_hwndVideo);
   }

   return hr;
}

CPlayer::CPlayer(HWND hVideo, HWND hEvent) : 
    m_pSession(NULL),
    m_pSource(NULL),
    m_pVideoDisplay(NULL),
    m_hwndVideo(hVideo),
    m_hwndEvent(hEvent),
    m_state(Closed),
    m_hCloseEvent(NULL),
    m_nRefCount(1),
	m_pEVRPresenter(NULL),
	m_pSequencerSource(NULL),
	m_pVolumeControl(NULL),
	m_previousTopoID(0),
	m_isLooping(false),
	m_width(0),
	m_height(0)
{

}

CPlayer::~CPlayer()
{
    assert(m_pSession == NULL);  
    // If FALSE, the app did not call Shutdown().

    // When CPlayer calls IMediaEventGenerator::BeginGetEvent on the
    // media session, it causes the media session to hold a reference 
    // count on the CPlayer. 
    
    // This creates a circular reference count between CPlayer and the 
    // media session. Calling Shutdown breaks the circular reference 
    // count.

    // If CreateInstance fails, the application will not call 
    // Shutdown. To handle that case, call Shutdown in the destructor. 


	if (v_EVRPresenters.size() >1) {
		SAFE_RELEASE(v_EVRPresenters[0]);
		SAFE_RELEASE(v_EVRPresenters[1]);
	}

    Shutdown();
	//SAFE_RELEASE(m_pEVRPresenter);
	SafeRelease(&m_pSequencerSource);
}

// IUnknown methods

HRESULT CPlayer::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] = 
    {
        QITABENT(CPlayer, IMFAsyncCallback),
        { 0 }
    };
    return QISearch(this, qit, riid, ppv);
}

ULONG CPlayer::AddRef()
{
    return InterlockedIncrement(&m_nRefCount);
}

ULONG CPlayer::Release()
{
    ULONG uCount = InterlockedDecrement(&m_nRefCount);
    if (uCount == 0)
    {
        delete this;
    }
    return uCount;
}

//  Open a URL for playback.
HRESULT CPlayer::OpenURL(const WCHAR *sURL)
{
    // Create a new media session.
	HRESULT hr = CreateSession();
    if (FAILED(hr))
    {
        goto done;
    }

    // Create the media source.
	hr = CreateMediaSource(sURL, &m_pSource);
    if (FAILED(hr))
    {
        goto done;
    }

	EndOpenURL();

done:
	if (FAILED(hr))
	{
		m_state = Closed;
	}
	return hr;
}

HRESULT CPlayer::OpenURLAsync(const WCHAR *sURL)
{
	// Create a new media session.
	HRESULT hr = CreateSession();
    if (FAILED(hr))
    {
        goto done;
    }

    // Create the media source.
	hr = BeginCreateMediaSource(sURL, this, &m_pSourceResolver);
    if (FAILED(hr))
    {
        goto done;
    }

	// Set async state and wait for callback to finish.
	m_state = OpenAsyncPending;

done:
	if (FAILED(hr))
	{
		m_state = Closed;
	}
	return hr;

}

HRESULT CPlayer::EndOpenURL()
{
	HRESULT hr;

	IMFTopology *pTopology = NULL;
	IMFPresentationDescriptor* pSourcePD = NULL;

	// Create the presentation descriptor for the media source.
	hr = m_pSource->CreatePresentationDescriptor(&pSourcePD);
	if (FAILED(hr))
	{
		goto done;
	}

	// Create a partial topology.
	hr = CreatePlaybackTopology(m_pSource, pSourcePD, m_hwndVideo, &pTopology, m_pEVRPresenter);
	if (FAILED(hr))
	{
		goto done;
	}

	SetMediaInfo(pSourcePD);

	// Set the topology on the media session.
	hr = m_pSession->SetTopology(0, pTopology);
	if (FAILED(hr))
	{
		goto done;
	}

	m_state = OpenPending;
	m_currentVolume = 1.0f;

	// If SetTopology succeeds, the media session will queue an 
	// MESessionTopologySet event.

done:
	if (FAILED(hr))
	{
		m_state = Closed;
	}

	SafeRelease(&pSourcePD);
	SafeRelease(&pTopology);
	return hr;
}

//  Pause playback.
HRESULT CPlayer::Pause()    
{
    if (m_state != Started)
    {
        return MF_E_INVALIDREQUEST;
    }
    if (m_pSession == NULL || m_pSource == NULL)
    {
        return E_UNEXPECTED;
    }

    HRESULT hr = m_pSession->Pause();
    if (SUCCEEDED(hr))
    {
        m_state = Paused;
    }

    return hr;
}

// Stop playback.
HRESULT CPlayer::Stop()
{
    if (m_state != Started && m_state != Paused)
    {
        return MF_E_INVALIDREQUEST;
    }
    if (m_pSession == NULL)
    {
        return E_UNEXPECTED;
    }

    HRESULT hr = m_pSession->Stop();
    if (SUCCEEDED(hr))
    {
        m_state = Stopped;
    }
    return hr;
}


HRESULT CPlayer::SetPosition(float pos)
{
	if (m_state == OpenPending)
	{
		ofLogError("ofWMFoundationPlayerUtils::setPosition") << "Cannot seek during opening";
		return S_FALSE;
	}

	//Create variant for seeking information
	PROPVARIANT varStart;
	PropVariantInit(&varStart);
	varStart.vt = VT_I8;
	varStart.hVal.QuadPart = pos* 10000000.0; //i.e. seeking to pos // should be MFTIME and not float :(

	HRESULT hr = m_pSession->Start(&GUID_NULL,&varStart);

	if (SUCCEEDED(hr))
	{
		m_state = Started;
	}
	else 
	{
		ofLogError("ofWMFoundationPlayerUtils::setPosition") << "Error while seeking";
		return S_FALSE;
	}

	PropVariantClear(&varStart);

	return S_OK;
}

HRESULT CPlayer::SetVolume(float vol)
{
	//Should we lock here as well ?
	if (m_pSession == NULL)
	{
		ofLogError("ofWMFoundationPlayerUtils::setVolume") << "Session is NULL";
		return E_FAIL;
	}
	if (m_pVolumeControl == NULL)
	{
		HRESULT hr = MFGetService(m_pSession, MR_STREAM_VOLUME_SERVICE, __uuidof(IMFAudioStreamVolume), (void**)&m_pVolumeControl);
		//HRESULT hr = MFGetService(m_pSession, MR_POLICY_VOLUME_SERVICE, __uuidof(IMFSimpleAudioVolume), (void**)&m_pVolumeControl);
		m_currentVolume = vol;
		if (FAILED(hr))
		{
			ofLogError("ofWMFoundationPlayerUtils::setVolume") << "Could not get sound control interface";
			return E_FAIL;
		}

	}
	UINT32 nChannels;
	m_pVolumeControl->GetChannelCount(&nChannels);
	for (int i = 0; i < nChannels; i++)
	{
		m_pVolumeControl->SetChannelVolume(i, vol);
	}

	m_currentVolume = vol;

	return S_OK;
}

//  Callback for the asynchronous BeginGetEvent method.
HRESULT CPlayer::Invoke(IMFAsyncResult *pResult)
{
    MediaEventType meType = MEUnknown;  // Event type
    IMFMediaEvent *pEvent = NULL;
	HRESULT hr;

	if (!m_pSession) {
		ofLogError("ofWMFoundationPlayerUtils::Invoke") << "Called with a null session";
		return -1;  // Sometimes Invoke is called but m_pSession is closed
	}

	// Get the event from the event queue.
	if (m_state == OpenAsyncPending) {
		if(!&m_pSourceResolver){
			ofLogError("ofWMFoundationPlayerUtils::Invoke") << "Async request returned with NULL session";
			return -1;
		}

		MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;
        IUnknown *pSourceUnk = NULL;

		//CheckPointer(m_pSource, E_POINTER);

		hr = m_pSourceResolver->EndCreateObjectFromURL(
				pResult,					// Invoke result
                &ObjectType,                // Receives the created object type. 
                &pSourceUnk);               // Receives a pointer to the media source.

        // Get the IMFMediaSource interface from the media source.
        if (SUCCEEDED(hr))
        {
            hr = pSourceUnk->QueryInterface(__uuidof(IMFMediaSource), (void**)(&m_pSource));
			m_state = OpenAsyncComplete; // Session finished opening URL
        }

		SafeRelease(&pSourceUnk);
		return hr;
	}
		
	// Get the event from the event queue.
    hr = m_pSession->EndGetEvent(pResult, &pEvent);
    if (FAILED(hr))
    {
        goto done;
    }

    // Get the event type. 
    hr = pEvent->GetType(&meType);
    if (FAILED(hr))
    {
        goto done;
    }

    if (meType == MESessionClosed)
    {
        // The session was closed. 
        // The application is waiting on the m_hCloseEvent event handle. 
        SetEvent(m_hCloseEvent);
    }
    else
    {
        // For all other events, get the next event in the queue.
        hr = m_pSession->BeginGetEvent(this, NULL);
        if (FAILED(hr))
        {
            goto done;
        }
    }

    // Check the application state. 
        
    // If a call to IMFMediaSession::Close is pending, it means the 
    // application is waiting on the m_hCloseEvent event and
    // the application's message loop is blocked. 

    // Otherwise, post a private window message to the application. 

    if (m_state != Closing)
    {
        // Leave a reference count on the event.
        pEvent->AddRef();

        PostMessage(m_hwndEvent, WM_APP_PLAYER_EVENT, 
            (WPARAM)pEvent, (LPARAM)meType);
    }

done:
    SafeRelease(&pEvent);
    return S_OK;
}

HRESULT CPlayer::HandleEvent(UINT_PTR pEventPtr)
{
    HRESULT hrStatus = S_OK;            
    MediaEventType meType = MEUnknown;  

    IMFMediaEvent *pEvent = (IMFMediaEvent*)pEventPtr;

    if (pEvent == NULL)
    {
        return E_POINTER;
    }

    // Get the event type.
    HRESULT hr = pEvent->GetType(&meType);
    if (FAILED(hr))
    {
        goto done;
    }

    // Get the event status. If the operation that triggered the event 
    // did not succeed, the status is a failure code.
    hr = pEvent->GetStatus(&hrStatus);

    // Check if the async operation succeeded.
    if (SUCCEEDED(hr) && FAILED(hrStatus)) 
    {
        hr = hrStatus;
    }
    if (FAILED(hr))
    {
        goto done;
    }

    switch(meType)
    {
    case MESessionTopologyStatus:
        hr = OnTopologyStatus(pEvent);
        break;

    case MEEndOfPresentation:
        hr = OnPresentationEnded(pEvent);
        break;

    case MENewPresentation:
        hr = OnNewPresentation(pEvent);
        break;

	case MESessionTopologySet:
		IMFTopology * topology;
		GetEventObject<IMFTopology> (pEvent,&topology);
		WORD nodeCount;
		topology->GetNodeCount(&nodeCount);
		ofLogVerbose("ofWMFoundationPlayerUtils::HandleEvent") << "Video topography set with " << nodeCount << " nodes";
		SafeRelease(&topology);
		break;

	case MESessionStarted:

		break;

	case MEBufferingStarted:
		cout << "MMM BUFFERING..." << endl;
		break;

	case MEBufferingStopped:
		cout << "MMM end BUFFERING..." << endl;
		break;

    default:
        hr = OnSessionEvent(pEvent, meType);
        break;
    }

done:
    SafeRelease(&pEvent);
    return hr;
}

HRESULT CPlayer::GetBufferDuration(DWORD *pDuration)
{
    IPropertyStore *pProp = NULL;
    PROPVARIANT var;

    // Get the property store from the media session.
    HRESULT hr = MFGetService(
        m_pSession, 
        MFNETSOURCE_STATISTICS_SERVICE, 
        IID_PPV_ARGS(&pProp)
        );

    if (SUCCEEDED(hr))
    {
        PROPERTYKEY key;
        key.fmtid = MFNETSOURCE_STATISTICS;
        key.pid = MFNETSOURCE_BUFFERSIZE_ID;

        hr = pProp->GetValue(key, &var);
    }

    if (SUCCEEDED(hr))
    {
        *pDuration = var.lVal;
    }
    PropVariantClear(&var);
    SafeRelease(&pProp);
    return hr;
}

HRESULT CPlayer::GetBufferProgress(DWORD *pProgress)
{
    IPropertyStore *pProp = NULL;
    PROPVARIANT var;

    // Get the property store from the media session.
    HRESULT hr = MFGetService(
        m_pSession, 
        MFNETSOURCE_STATISTICS_SERVICE, 
        IID_PPV_ARGS(&pProp)
        );

    if (SUCCEEDED(hr))
    {
        PROPERTYKEY key;
        key.fmtid = MFNETSOURCE_STATISTICS;
        key.pid = MFNETSOURCE_BUFFERPROGRESS_ID;

        hr = pProp->GetValue(key, &var);
    }

    if (SUCCEEDED(hr))
    {
        *pProgress = var.lVal;
//		cout << "buff prog " << *pProgress << endl;
    }
    PropVariantClear(&var);
    SafeRelease(&pProp);
    return hr;
}

//  Release all resources held by this object.
HRESULT CPlayer::Shutdown()
{
    // Close the session
	HRESULT hr = CloseSession();

    // Shutdown the Media Foundation platform
    if (m_hCloseEvent)
    {
        CloseHandle(m_hCloseEvent);
        m_hCloseEvent = NULL;
    }

	if (v_EVRPresenters.size() > 0)
	{
		if (v_EVRPresenters[0]) v_EVRPresenters[0]->releaseSharedTexture();
		if (v_EVRPresenters[1]) v_EVRPresenters[1]->releaseSharedTexture();

		SafeRelease(&v_EVRPresenters[0]);
		SafeRelease(&v_EVRPresenters[1]);
	}
	else
	{
		if (m_pEVRPresenter) { m_pEVRPresenter->releaseSharedTexture(); }
		SafeRelease(&m_pEVRPresenter);
	}

    return hr;
}

/// Protected methods

HRESULT CPlayer::OnTopologyStatus(IMFMediaEvent *pEvent)
{
    UINT32 status; 
    HRESULT hr = pEvent->GetUINT32(MF_EVENT_TOPOLOGY_STATUS, &status);

    if (SUCCEEDED(hr) && (status == MF_TOPOSTATUS_READY))
    {
        SafeRelease(&m_pVideoDisplay);

        hr = StartPlayback();
		hr = Pause();
    }

    return hr;
}

//  Handler for MEEndOfPresentation event.
HRESULT CPlayer::OnPresentationEnded(IMFMediaEvent *pEvent)
{
	m_pSession->Pause();
	m_state = Paused;

	//Create variant for seeking information
	PROPVARIANT varStart;
	PropVariantInit(&varStart);
	varStart.vt = VT_I8;
	varStart.hVal.QuadPart = 0; //i.e. seeking to the beginning

	HRESULT hr = S_OK;
	hr = m_pSession->Start(&GUID_NULL,&varStart);

	if FAILED(hr)
	{
		ofLogError("ofWMFoundationPlayerUtils", "Error while looping");
	}
	if (!m_isLooping) m_pSession->Pause();
	else m_state = Started;

	PropVariantClear(&varStart);

	// The session puts itself into the stopped state automatically.
	// else m_state = Stopped;
	return S_OK;
}

//  Handler for MENewPresentation event.
//
//  This event is sent if the media source has a new presentation, which 
//  requires a new topology. 

HRESULT CPlayer::OnNewPresentation(IMFMediaEvent *pEvent)
{
	IMFPresentationDescriptor *pPD = NULL;
	IMFTopology *pTopology = NULL;

	// Get the presentation descriptor from the event.
	HRESULT hr = GetEventObject(pEvent, &pPD);
	if (FAILED(hr))
	{
		goto done;
	}

	// Create a partial topology.
	hr = CreatePlaybackTopology(m_pSource, pPD,  m_hwndVideo,&pTopology,m_pEVRPresenter);
	if (FAILED(hr))
	{
		goto done;
	}

	SetMediaInfo(pPD);

	// Set the topology on the media session.
	hr = m_pSession->SetTopology(0, pTopology);
	if (FAILED(hr))
	{
		goto done;
	}

	m_state = OpenPending;

done:
	SafeRelease(&pTopology);
	SafeRelease(&pPD);
	return S_OK;
}

//  Create a new instance of the media session.
HRESULT CPlayer::CreateSession()
{
	// Close the old session, if any.
	HRESULT hr = CloseSession();
	if (FAILED(hr))
	{
		goto done;
	}

	assert(m_state == Closed);

	// Create the media session.
	hr = MFCreateMediaSession(NULL, &m_pSession);
	if (FAILED(hr))
	{
		goto done;
	}

	// Start pulling events from the media session
	hr = m_pSession->BeginGetEvent((IMFAsyncCallback*)this, NULL);
	if (FAILED(hr))
	{
		goto done;
	}

	m_state = Ready;

done:
	return hr;
}

//  Close the media session. 
HRESULT CPlayer::CloseSession()
{
	//  The IMFMediaSession::Close method is asynchronous, but the 
	//  CPlayer::CloseSession method waits on the MESessionClosed event.
	//  
	//  MESessionClosed is guaranteed to be the last event that the 
	//  media session fires.

	HRESULT hr = S_OK;

	if (m_pVideoDisplay != NULL ) SafeRelease(&m_pVideoDisplay);
	if (m_pVolumeControl != NULL) SafeRelease(&m_pVolumeControl);

	// First close the media session.
	if (m_pSession)
	{
		DWORD dwWaitResult = 0;

		m_state = Closing;

		hr = m_pSession->Close();
		// Wait for the close operation to complete
		if (SUCCEEDED(hr))
		{
			dwWaitResult = WaitForSingleObject(m_hCloseEvent, 5000);
			if (dwWaitResult == WAIT_TIMEOUT)
			{
				assert(FALSE);
			}
			// Now there will be no more events from this session.
		}
	}

	// Complete shutdown operations.
	if (SUCCEEDED(hr))
	{
		// Shut down the media source. (Synchronous operation, no events.)
		if (m_pSource)
		{
			(void)m_pSource->Shutdown();
		}
		// Shut down the media session. (Synchronous operation, no events.)
		if (m_pSession)
		{
			(void)m_pSession->Shutdown();
		}
	}

	SafeRelease(&m_pSource);
	SafeRelease(&m_pSession);
	m_state = Closed;
	return hr;
}

//  Start playback from the current position. 
HRESULT CPlayer::StartPlayback()
{
    assert(m_pSession != NULL);

    PROPVARIANT varStart;
    PropVariantInit(&varStart);

    HRESULT hr = m_pSession->Start(&GUID_NULL, &varStart);
    if (SUCCEEDED(hr))
    {
        // Note: Start is an asynchronous operation. However, we
        // can treat our state as being already started. If Start
        // fails later, we'll get an MESessionStarted event with
        // an error code, and we will update our state then.
        m_state = Started;
    }
	
    PropVariantClear(&varStart);
    return hr;
}

//  Start playback from paused or stopped.
HRESULT CPlayer::Play()
{
    if (m_state != Paused && m_state != Stopped)
    {
        return MF_E_INVALIDREQUEST;
    }

    if (m_pSession == NULL || m_pSource == NULL)
    {
        return E_UNEXPECTED;
    }

    return StartPlayback();
}

//  Create an activation object for a renderer, based on the stream media type.

HRESULT CreateMediaSinkActivate(
    IMFStreamDescriptor *pSourceSD,     // Pointer to the stream descriptor.
    HWND hVideoWindow,                  // Handle to the video clipping window.
    IMFActivate **ppActivate,
	IMFVideoPresenter *pVideoPresenter,
	IMFMediaSink **ppMediaSink)
{
    IMFMediaTypeHandler *pHandler = NULL;
    IMFActivate *pActivate = NULL;
	IMFMediaSink *pSink = NULL;

    // Get the media type handler for the stream.
    HRESULT hr = pSourceSD->GetMediaTypeHandler(&pHandler);
    if (FAILED(hr))
    {
        goto done;
    }

    // Get the major media type.
    GUID guidMajorType;
    hr = pHandler->GetMajorType(&guidMajorType);
    if (FAILED(hr))
    {
        goto done;
    }
 
    // Create an IMFActivate object for the renderer, based on the media type.
    if (MFMediaType_Audio == guidMajorType)
    {
        // Create the audio renderer.
        hr = MFCreateAudioRendererActivate(&pActivate);
		*ppActivate = pActivate;
		(*ppActivate)->AddRef();
    }
    else if (MFMediaType_Video == guidMajorType)
    {
        // Create the video renderer.
       // hr = MFCreateVideoRendererActivate(hVideoWindow, &pActivate);
		hr = MFCreateVideoRenderer( __uuidof(IMFMediaSink), (void**)&pSink);

		IMFVideoRenderer* pVideoRenderer = NULL;
		hr = pSink->QueryInterface(__uuidof(IMFVideoRenderer), (void**) &pVideoRenderer);

		//ThrowIfFail( pVideoRenderer->InitializeRenderer( NULL, pVideoPresenter ) );
		hr = pVideoRenderer->InitializeRenderer(NULL, pVideoPresenter);

		*ppMediaSink = pSink;
		(*ppMediaSink)->AddRef();
    }
    else
    {
        // Unknown stream type. 
        hr = E_FAIL;
        // Optionally, you could deselect this stream instead of failing.
    }
    if (FAILED(hr))
    {
        goto done;
    }
 
    // Return IMFActivate pointer to caller.

done:
    SafeRelease(&pHandler);
    SafeRelease(&pActivate);
	SafeRelease(&pSink);
    return hr;
}

// Add a source node to a topology.
HRESULT AddSourceNode(
    IMFTopology *pTopology,           // Topology.
    IMFMediaSource *pSource,          // Media source.
    IMFPresentationDescriptor *pPD,   // Presentation descriptor.
    IMFStreamDescriptor *pSD,         // Stream descriptor.
    IMFTopologyNode **ppNode)         // Receives the node pointer.
{
    IMFTopologyNode *pNode = NULL;

    // Create the node.
    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &pNode);
    if (FAILED(hr))
    {
        goto done;
    }

    // Set the attributes.
    hr = pNode->SetUnknown(MF_TOPONODE_SOURCE, pSource);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pPD);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, pSD);
    if (FAILED(hr))
    {
        goto done;
    }
    
    // Add the node to the topology.
    hr = pTopology->AddNode(pNode);
    if (FAILED(hr))
    {
        goto done;
    }

    // Return the pointer to the caller.
    *ppNode = pNode;
    (*ppNode)->AddRef();

done:
    SafeRelease(&pNode);
    return hr;
}

HRESULT AddOutputNode(
	IMFTopology *pTopology,     // Topology.
	IMFStreamSink *pStreamSink, // Stream sink.
	IMFTopologyNode **ppNode)   // Receives the node pointer.
{
	IMFTopologyNode *pNode = NULL;
	HRESULT hr = S_OK;

	// Create the node.
	hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pNode);

	// Set the object pointer.
	if (SUCCEEDED(hr))
	{
		hr = pNode->SetObject(pStreamSink);
	}

	// Add the node to the topology.
	if (SUCCEEDED(hr))
	{
		hr = pTopology->AddNode(pNode);
	}

	if (SUCCEEDED(hr))
	{
		hr = pNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, TRUE);
	}

	// Return the pointer to the caller.
	if (SUCCEEDED(hr))
	{
		*ppNode = pNode;
		(*ppNode)->AddRef();
	}

	if (pNode)
	{
		pNode->Release();
	}
	return hr;
}


// Add an output node to a topology.
HRESULT AddOutputNode(
    IMFTopology *pTopology,     // Topology.
    IMFActivate *pActivate,     // Media sink activation object.
    DWORD dwId,                 // Identifier of the stream sink.
    IMFTopologyNode **ppNode)   // Receives the node pointer.
{
    IMFTopologyNode *pNode = NULL;

    // Create the node.
    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pNode);
    if (FAILED(hr))
    {
        goto done;
    }

    // Set the object pointer.
    hr = pNode->SetObject(pActivate);
    if (FAILED(hr))
    {
        goto done;
    }

    // Set the stream sink ID attribute.
    hr = pNode->SetUINT32(MF_TOPONODE_STREAMID, dwId);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE);
    if (FAILED(hr))
    {
        goto done;
    }

    // Add the node to the topology.
    hr = pTopology->AddNode(pNode);
    if (FAILED(hr))
    {
        goto done;
    }

    // Return the pointer to the caller.
    *ppNode = pNode;
    (*ppNode)->AddRef();

done:
    SafeRelease(&pNode);
    return hr;
}
//</SnippetPlayer.cpp>

//  Add a topology branch for one stream.
//
//  For each stream, this function does the following:
//
//    1. Creates a source node associated with the stream. 
//    2. Creates an output node for the renderer. 
//    3. Connects the two nodes.
//
//  The media session will add any decoders that are needed.

HRESULT AddBranchToPartialTopology(
    IMFTopology *pTopology,         // Topology.
    IMFMediaSource *pSource,        // Media source.
    IMFPresentationDescriptor *pPD, // Presentation descriptor.
    DWORD iStream,                  // Stream index.
    HWND hVideoWnd,
	IMFVideoPresenter *pVideoPresenter)                 // Window for video playback.
{
    IMFStreamDescriptor *pSD = NULL;
    IMFActivate         *pSinkActivate = NULL;
    IMFTopologyNode     *pSourceNode = NULL;
    IMFTopologyNode     *pOutputNode = NULL;
	IMFMediaSink        *pMediaSink = NULL;

    BOOL fSelected = FALSE;

    HRESULT hr = pPD->GetStreamDescriptorByIndex(iStream, &fSelected, &pSD);
    if (FAILED(hr))
    {
        goto done;
    }

    if (fSelected)
    {
        // Create the media sink activation object.
        hr = CreateMediaSinkActivate(pSD, hVideoWnd, &pSinkActivate, pVideoPresenter, &pMediaSink);
        if (FAILED(hr))
        {
            goto done;
        }

        // Add a source node for this stream.
        hr = AddSourceNode(pTopology, pSource, pPD, pSD, &pSourceNode);
        if (FAILED(hr))
        {
            goto done;
        }

        // Create the output node for the renderer.
		if (pSinkActivate)
		{
			 hr = AddOutputNode(pTopology, pSinkActivate, 0, &pOutputNode);
		}
		else if (pMediaSink)
		{
			IMFStreamSink  * pStreamSink = NULL;
			DWORD streamCount;

			pMediaSink->GetStreamSinkCount(&streamCount);
			pMediaSink->GetStreamSinkByIndex(0, &pStreamSink);

			hr = AddOutputNode(pTopology, pStreamSink, &pOutputNode);

			//ThrowIfFail( pNode->SetObject( pStreamSink ) );
		}
        if (FAILED(hr))
        {
            goto done;
        }

        // Connect the source node to the output node.
        hr = pSourceNode->ConnectOutput(0, pOutputNode, 0);
    }
    // else: If not selected, don't add the branch. 

done:
    SafeRelease(&pSD);
    SafeRelease(&pSinkActivate);
    SafeRelease(&pSourceNode);
    SafeRelease(&pOutputNode);
	
    return hr;
}

//  Create a playback topology from a media source.
HRESULT CreatePlaybackTopology(
    IMFMediaSource *pSource,          // Media source.
    IMFPresentationDescriptor *pPD,   // Presentation descriptor.
    HWND hVideoWnd,                   // Video window.
    IMFTopology **ppTopology,         // Receives a pointer to the topology.
	IMFVideoPresenter *pVideoPresenter)         
{
    IMFTopology *pTopology = NULL;
    DWORD cSourceStreams = 0;

    // Create a new topology.
    HRESULT hr = MFCreateTopology(&pTopology);
    if (FAILED(hr))
    {
        goto done;
    }

    // Get the number of streams in the media source.
    hr = pPD->GetStreamDescriptorCount(&cSourceStreams);
    if (FAILED(hr))
    {
        goto done;
    }
	
    // For each stream, create the topology nodes and add them to the topology.
    for (DWORD i = 0; i < cSourceStreams; i++)
    {
        hr = AddBranchToPartialTopology(pTopology, pSource, pPD, i, hVideoWnd, pVideoPresenter);
        if (FAILED(hr))
        {
            goto done;
        }
    }
	
    // Return the IMFTopology pointer to the caller.
    *ppTopology = pTopology;
    (*ppTopology)->AddRef();

done:
    SafeRelease(&pTopology);
    return hr;
}

HRESULT AddToPlaybackTopology(
	IMFMediaSource *pSource,          // Media source.
	IMFPresentationDescriptor *pPD,   // Presentation descriptor.
	HWND hVideoWnd,                   // Video window.
	IMFTopology *pTopology,           // Receives a pointer to the topology.
	IMFVideoPresenter *pVideoPresenter)         
{
	DWORD cSourceStreams = 0;

	HRESULT hr;

	// Get the number of streams in the media source.
	hr = pPD->GetStreamDescriptorCount(&cSourceStreams);
	if (FAILED(hr))
	{
		goto done;
	}

	// For each stream, create the topology nodes and add them to the topology.
	for (DWORD i = 1; i < cSourceStreams; i++)
	{
		ofLogWarning("ofWMFoundationPlayerUtils::AddToPlaybackTopology") << "Ignoring audio stream of video2. If the video is missing check ofWMFoundationPlayerUtils";
		hr = AddBranchToPartialTopology(pTopology, pSource, pPD, i, hVideoWnd, pVideoPresenter);
		if (FAILED(hr))
		{
			goto done;
		}
	}

done:
	return hr;
}

///------------ 
/// Extra functions
//---------------

float CPlayer::GetDuration() 
{
	if (m_pSource == NULL) {
		return 0.0;
	}

	float duration = 0.0;
	IMFPresentationDescriptor *pDescriptor = NULL;
	HRESULT hr = m_pSource->CreatePresentationDescriptor(&pDescriptor);
	if (SUCCEEDED(hr)) {
		UINT64 longDuration = 0;
		hr = pDescriptor->GetUINT64(MF_PD_DURATION, &longDuration);
		if (SUCCEEDED(hr)) {
			duration = (float)(longDuration / 10000000.0);
		}
	}
	SafeRelease(&pDescriptor);
	return duration;
}

float CPlayer::GetPosition() 
{
	if (m_pSession == NULL) {
		return 0.0;
	}
	
	float position = 0.0;
	IMFPresentationClock *pClock = NULL;
	HRESULT hr = m_pSession->GetClock((IMFClock **)&pClock);
	if (SUCCEEDED(hr)) {
		MFTIME longPosition = 0;
		hr = pClock->GetTime(&longPosition);
		if (SUCCEEDED(hr)) {
			position = (float)(longPosition / 10000000.0);
		}
	}
	SafeRelease(&pClock);
	return position;
}

float CPlayer::GetFrameRate() 
{
	if (m_pSource == NULL) {
		return 0.0;
	}

	float fps = 0.0;
	IMFPresentationDescriptor *pDescriptor = NULL;
	IMFStreamDescriptor *pStreamHandler = NULL;
	IMFMediaTypeHandler *pMediaType = NULL;
	IMFMediaType  *pType;
	DWORD nStream;
	if FAILED(m_pSource->CreatePresentationDescriptor(&pDescriptor)) goto done;
	if FAILED(pDescriptor->GetStreamDescriptorCount(&nStream)) goto done;
	for (int i = 0; i < nStream; i++)
	{
		BOOL selected;
		GUID type;
		if FAILED(pDescriptor->GetStreamDescriptorByIndex(i, &selected, &pStreamHandler)) goto done;
		if FAILED(pStreamHandler->GetMediaTypeHandler(&pMediaType)) goto done;
		if FAILED(pMediaType->GetMajorType(&type)) goto done;
		if FAILED(pMediaType->GetCurrentMediaType(&pType)) goto done;
		if (type  == MFMediaType_Video)
		{
			UINT32 num = 0;
			UINT32 denum = 1;

			MFGetAttributeRatio(
				pType,
				MF_MT_FRAME_RATE,
				&num,
				&denum);
			if (denum != 0) fps = (float) num /  (float) denum;
		}

		SafeRelease(&pStreamHandler);
		SafeRelease(&pMediaType);
		SafeRelease(&pType);

		if (fps != 0.0) break;  // We found the right stream, no point in continuing the loop
	}

done:
	SafeRelease(&pDescriptor);
	SafeRelease(&pStreamHandler);
	SafeRelease(&pMediaType);
	SafeRelease(&pType);

	return fps;
}

HRESULT CPlayer::SetMediaInfo( IMFPresentationDescriptor *pPD ) 
{
	m_width = 0;
	m_height = 0;
	HRESULT hr = S_OK;
	GUID guidMajorType = GUID_NULL;
	IMFMediaTypeHandler *pHandler = NULL;
	IMFStreamDescriptor* spStreamDesc = NULL;
	IMFMediaType *sourceType = NULL;

	DWORD count;
	pPD->GetStreamDescriptorCount(&count);
	for (DWORD i = 0; i < count; i++) {
		BOOL selected;
		
		hr = pPD->GetStreamDescriptorByIndex(i, &selected, &spStreamDesc);
		if (FAILED(hr)) goto done;
		if (selected) {
			hr = spStreamDesc->GetMediaTypeHandler(&pHandler);
			if (FAILED(hr)) goto done;
			hr = pHandler->GetMajorType(&guidMajorType);
			if (FAILED(hr)) goto done;
			
			if (MFMediaType_Video == guidMajorType) {
				// first get the source video size and allocate a new texture
				hr = pHandler->GetCurrentMediaType(&sourceType) ;

				UINT32 w, h;
				hr = MFGetAttributeSize(sourceType, MF_MT_FRAME_SIZE, &w, &h);
				if (hr == S_OK) {
					m_width = w;
					m_height =h;
				}

				goto done;
			}
		}
	}

done:
	SafeRelease(&sourceType);
	SafeRelease(&pHandler);
	SafeRelease(&spStreamDesc);

	return hr;
}

///////////////////////////////////////////////////////////////////////
//  Name: SetPlaybackRate
//  Description: 
//      Gets the rate control service from Media Session.
//      Sets the playback rate to the specified rate.
//  Parameter:
//      pMediaSession: [in] Media session object to query.
//      rateRequested: [in] Playback rate to set.
//      bThin: [in] Indicates whether to use thinning.
///////////////////////////////////////////////////////////////////////
// taken from MSDN example on rate control

HRESULT CPlayer::SetPlaybackRate(BOOL bThin, float rateRequested)
{
	HRESULT hr = S_OK;
	IMFRateControl *pRateControl = NULL;

	// Get the rate control object from the Media Session.
	hr = MFGetService(
		m_pSession,
		MF_RATE_CONTROL_SERVICE,
		IID_IMFRateControl,
		(void**)&pRateControl);

	// Set the playback rate.
	if (SUCCEEDED(hr))
	{
		hr = pRateControl->SetRate(bThin, rateRequested);
	}

	// Clean up.
	SAFE_RELEASE(pRateControl);

	return hr;
}

float CPlayer::GetPlaybackRate()
{
	HRESULT hr = S_OK;
	IMFRateControl *pRateControl = NULL;
	BOOL bThin;
	float rate;

	// Get the rate control object from the Media Session.
	hr = MFGetService(
		m_pSession,
		MF_RATE_CONTROL_SERVICE,
		IID_IMFRateControl,
		(void**)&pRateControl);

	// Set the playback rate.
	if (SUCCEEDED(hr))
	{
		hr = pRateControl->GetRate(&bThin, &rate);
	}

	// Clean up.
	SAFE_RELEASE(pRateControl);
	if (!FAILED(hr)) {
		return rate;
	}
	else {
		ofLogError("ofWMFoundationPlayerUtils::GetPlaybackRate") << "Could not get playback rate.";
		return NULL;
	}
}
