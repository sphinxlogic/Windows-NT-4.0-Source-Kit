//
// SignerInfoCom.cpp
//
// COM plumbing for CSignerInfo implementation
//

#include "stdpch.h"
#include "common.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////

HRESULT CSignerInfo::CreateInstance(
			IUnknown*	punkOuter,
			SignerInfo*	pSignerInfo,
			CPkcs7*		pseven,
			REFIID		iid, 
			void**		ppv)
	{
	HRESULT hr;
	ASSERT(punkOuter == NULL || iid == IID_IUnknown);
	*ppv = NULL;
	CSignerInfo* pnew = new CSignerInfo(punkOuter, pSignerInfo);
 	if (pnew == NULL) return E_OUTOFMEMORY;
	if ((hr = pnew->Init(pseven)) != S_OK)
		{
		delete pnew;
		return hr;
		}
	IUnkInner* pme = (IUnkInner*)pnew;
	hr = pme->InnerQueryInterface(iid, ppv);
	pme->InnerRelease();				// balance starting ref cnt of one
	return hr;
	}

CSignerInfo::CSignerInfo(IUnknown* punkOuter, SignerInfo* pSignerInfo) :
		 m_refs(1),						// Note: start with reference count of one
         m_coloredRefs(0),
		 m_pSignerInfo(pSignerInfo),
		 m_pseven(NULL),
		 m_cAttrsActive(0)
	{
    NoteObjectBirth();
	if (punkOuter == NULL)
		m_punkOuter = (IUnknown *) ((LPVOID) ((IUnkInner *) this));
	else
		m_punkOuter = punkOuter;
	}

CSignerInfo::~CSignerInfo(void)
	{
	Free();
    NoteObjectDeath();
	}

/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CSignerInfo::InnerQueryInterface(REFIID iid, LPVOID* ppv)
	{
	*ppv = NULL;
	
	while (TRUE)
		{
		if (iid == IID_IUnknown)
			{
			*ppv = (LPVOID)((IUnkInner*)this);
			break;
			}
		if (iid == IID_ISignerInfo)
			{
			*ppv = (LPVOID) ((ISignerInfo *) this);
			break;
			}
		if (iid == IID_IPersistMemory)
			{
			*ppv = (LPVOID) ((IPersistMemory *) this);
			break;
			}
		if (iid == IID_IAmHashed || iid == IID_IAmSigned)
			{
			*ppv = (LPVOID) ((IAmSigned *) this);
			break;
			}
		if (iid == IID_IColoredRef)
			{
			*ppv = (LPVOID) ((IColoredRef *) this);
			break;
			}
		
		// From our CPersistGlue helper
		if (iid == IID_IPersistStream)
			return m_punkPersistStream->QueryInterface(iid, ppv);
		if (iid == IID_IPersistFile)
			return m_punkPersistFile->QueryInterface(iid, ppv);
		return E_NOINTERFACE;
		}
	((IUnknown*)*ppv)->AddRef();
	return S_OK;
	}

STDMETHODIMP_(ULONG) CSignerInfo::InnerAddRef(void)
	{
	return ++m_refs;
	}

STDMETHODIMP CSignerInfo::ColoredAddRef(REFGUID guidColor)
    {
    if (guidColor == guidOurColor)
        {
        ++m_coloredRefs;
        return S_OK;
        }
    else
        return E_INVALIDARG;
    }

STDMETHODIMP_(ULONG) CSignerInfo::InnerRelease(void)
	{
	ULONG refs = --m_refs;
    //
    // If all of our external clients are gone then just live to service
    // the internal ones. Which means we don't have to service the internal
    // ones any longer. So release them.
    //
	if (refs == 0)
		{
        //
        // Release any state holding internal stuff alive. None for this class
        //
        if (m_coloredRefs==0)
            {
            m_refs = 1;
            delete this;
            }
		}
	return refs;
	}
STDMETHODIMP CSignerInfo::ColoredRelease(REFGUID guidColor)
    {
    if (guidColor == guidOurColor)
        {
        if (--m_coloredRefs==0 && m_refs==0)
            {
            m_refs = 1;
            delete this;
            }
        return S_OK;
        }
    else
        return E_INVALIDARG;
    }

/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CSignerInfo::QueryInterface(REFIID iid, LPVOID* ppv)
	{
	return (m_punkOuter->QueryInterface(iid, ppv));		
	}

STDMETHODIMP_(ULONG) CSignerInfo::AddRef(void)
	{
	return (m_punkOuter->AddRef());
	}

STDMETHODIMP_(ULONG) CSignerInfo::Release(void)
	{
	return (m_punkOuter->Release());
	}

