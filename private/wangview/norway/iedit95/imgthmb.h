#ifndef __IMGTHUMBNAIL_H__
#define __IMGTHUMBNAIL_H__

// Machine generated IDispatch wrapper class(es) created by Microsoft Visual C++

// NOTE: Do not modify the contents of this file.  If this class is regenerated by
//  Microsoft Visual C++, your modifications will be overwritten.

/////////////////////////////////////////////////////////////////////////////
// CImgThumbnail wrapper class

class CImgThumbnail : public CWnd
{
protected:
	DECLARE_DYNCREATE(CImgThumbnail)
public:
	CLSID const& GetClsid()
	{
		static CLSID const clsid
			= { 0xe1a6b8a0, 0x3603, 0x101c, { 0xac, 0x6e, 0x4, 0x2, 0x24, 0x0, 0x9c, 0x2 } };
		return clsid;
	}
	virtual BOOL Create(LPCTSTR lpszClassName,
		LPCTSTR lpszWindowName, DWORD dwStyle,
		const RECT& rect,
		CWnd* pParentWnd, UINT nID,
		CCreateContext* pContext = NULL)
	{ return CreateControl(GetClsid(), lpszWindowName, dwStyle, rect, pParentWnd, nID); }

    BOOL Create(LPCTSTR lpszWindowName, DWORD dwStyle,
		const RECT& rect, CWnd* pParentWnd, UINT nID,
		CFile* pPersist = NULL, BOOL bStorage = FALSE,
		BSTR bstrLicKey = NULL)
	{ return CreateControl(GetClsid(), lpszWindowName, dwStyle, rect, pParentWnd, nID,
		pPersist, bStorage, bstrLicKey); }

// Attributes
public:
	long GetThumbCount();
	void SetThumbCount(long);
	long GetThumbWidth();
	void SetThumbWidth(long);
	long GetThumbHeight();
	void SetThumbHeight(long);
	long GetScrollDirection();
	void SetScrollDirection(long);
	long GetThumbCaptionStyle();
	void SetThumbCaptionStyle(long);
	unsigned long GetThumbCaptionColor();
	void SetThumbCaptionColor(unsigned long);
	LPDISPATCH GetThumbCaptionFont();
	void SetThumbCaptionFont(LPDISPATCH);
	BOOL GetHighlightSelectedThumbs();
	void SetHighlightSelectedThumbs(BOOL);
	long GetSelectedThumbCount();
	void SetSelectedThumbCount(long);
	long GetFirstSelectedThumb();
	void SetFirstSelectedThumb(long);
	long GetLastSelectedThumb();
	void SetLastSelectedThumb(long);
	CString GetThumbCaption();
	void SetThumbCaption(LPCTSTR);
	unsigned long GetHighlightColor();
	void SetHighlightColor(unsigned long);
	unsigned long GetThumbBackColor();
	void SetThumbBackColor(unsigned long);
	long GetStatusCode();
	void SetStatusCode(long);
	CString GetImage();
	void SetImage(LPCTSTR);
	long GetMousePointer();
	void SetMousePointer(long);
	LPDISPATCH GetMouseIcon();
	void SetMouseIcon(LPDISPATCH);
	OLE_COLOR GetBackColor();
	void SetBackColor(OLE_COLOR);
	short GetBorderStyle();
	void SetBorderStyle(short);
	BOOL GetEnabled();
	void SetEnabled(BOOL);
	OLE_HANDLE GetHWnd();
	void SetHWnd(OLE_HANDLE);
	long GetFirstDisplayedThumb();
	void SetFirstDisplayedThumb(long);
	long GetLastDisplayedThumb();
	void SetLastDisplayedThumb(long);

// Operations
public:
	void SelectAllThumbs();
	void DeselectAllThumbs();
	long GetMinimumSize(long ThumbCount, BOOL ScrollBar);
	long GetMaximumSize(long ThumbCount, BOOL ScrollBar);
	void ClearThumbs(const VARIANT& PageNumber);
	void InsertThumbs(const VARIANT& InsertBeforeThumb, const VARIANT& InsertCount);
	void DeleteThumbs(long DeleteAt, const VARIANT& DeleteCount);
	void DisplayThumbs(const VARIANT& ThumbNumber, const VARIANT& Option);
	void GenerateThumb(short Option, const VARIANT& PageNumber);
	BOOL ScrollThumbs(short Direction, short Amount);
	BOOL UISetThumbSize(const VARIANT& Image, const VARIANT& Page);
	long GetScrollDirectionSize(long ScrollDirectionThumbCount, long NonScrollDirectionThumbCount, long NonScrollDirectionSize, BOOL ScrollBar);
	void Refresh();
	long GetThumbPositionX(long ThumbNumber);
	long GetThumbPositionY(long ThumbNumber);
	CString GetVersion();
	BOOL GetThumbSelected(long PageNumber);
	void SetThumbSelected(long PageNumber, BOOL bNewValue);
	void AboutBox();
};

#endif // __IMGTHUMBNAIL_H__
