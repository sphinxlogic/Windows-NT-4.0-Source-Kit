/* SCCIO_OL.C 05/08/94 09.54.22 */
IOERR IOOpenRootStorageNP (HIOFILE FAR *phFile, HIOSPEC hSpec, DWORD dwFlags,
	 VOID FAR *pPath);
IOERR IOOpenSubStorageNP (HIOFILE FAR *phFile, HIOSPEC hSpec, DWORD dwFlags,
	 PIOSPECSUBSTORAGE pSubStorage);
IOERR IOOpenSubStreamNP (HIOFILE FAR *phFile, HIOSPEC hSpec, DWORD dwFlags,
	 PIOSPECSUBSTREAM pSubStream);
IOERR IOOpenIStreamNP (HIOFILE FAR *phFile, LPVOID pStr, DWORD dwFlags);
IOERR IOOpenIStorageNP (HIOFILE FAR *phFile, LPVOID pStg, DWORD dwFlags);
IOERR IO_ENTRYMOD IOStgCloseNP (HIOFILE hFile);
IOERR IO_ENTRYMOD IOStgReadNP (HIOFILE hFile, BYTE FAR *pData, DWORD dwSize,
	 DWORD FAR *pCount);
IOERR IO_ENTRYMOD IOStgWriteNP (HIOFILE hFile, BYTE FAR *pData, DWORD dwSize,
	 DWORD FAR *pCount);
IOERR IO_ENTRYMOD IOStgSeekNP (HIOFILE hFile, WORD wFrom, LONG lOffset);
IOERR IO_ENTRYMOD IOStgTellNP (HIOFILE hFile, DWORD FAR *pOffset);
IOERR IO_ENTRYMOD IOStgGetInfoNP (HIOFILE hFile, DWORD dwInfoId, VOID FAR *
	pInfo);
IOERR IO_ENTRYMOD IOStrCloseNP (HIOFILE hFile);
IOERR IO_ENTRYMOD IOStrReadNP (HIOFILE hFile, BYTE FAR *pData, DWORD dwSize,
	 DWORD FAR *pCount);
IOERR IO_ENTRYMOD IOStrWriteNP (HIOFILE hFile, BYTE FAR *pData, DWORD dwSize,
	 DWORD FAR *pCount);
IOERR IO_ENTRYMOD IOStrSeekNP (HIOFILE hFile, WORD wFrom, LONG lOffset);
IOERR IO_ENTRYMOD IOStrTellNP (HIOFILE hFile, DWORD FAR *pOffset);
IOERR IO_ENTRYMOD IOStrGetInfoNP (HIOFILE hFile, DWORD dwInfoId, VOID FAR *
	pInfo);
HRESULT STDMETHODCALLTYPE LBQueryInterface (LPLOCKBYTES pLockBytes, REFIID riid
	, LPVOID FAR *ppvObj);
DWORD STDMETHODCALLTYPE LBAddRef (LPLOCKBYTES pLockBytes);
DWORD STDMETHODCALLTYPE LBRelease (LPLOCKBYTES pLockBytes);
HRESULT STDMETHODCALLTYPE LBReadAt (LPLOCKBYTES pLockBytes, ULARGE_INTEGER
	 ulOffset, VOID HUGEP *pv, ULONG cb, ULONG FAR *pcbRead);
HRESULT STDMETHODCALLTYPE LBWriteAt (LPLOCKBYTES pLockBytes, ULARGE_INTEGER
	 ulOffset, VOID const HUGEP *pv, ULONG cb, ULONG FAR *pcbWritten);
HRESULT STDMETHODCALLTYPE LBFlush (LPLOCKBYTES pLockBytes);
HRESULT STDMETHODCALLTYPE LBSetSize (LPLOCKBYTES pLockBytes, ULARGE_INTEGER cb
	);
HRESULT STDMETHODCALLTYPE LBLockRegion (LPLOCKBYTES pLockBytes, ULARGE_INTEGER
	 libOffset, ULARGE_INTEGER cb, DWORD dwLockType);
HRESULT STDMETHODCALLTYPE LBUnlockRegion (LPLOCKBYTES pLockBytes,
	 ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType);
HRESULT STDMETHODCALLTYPE LBStat (LPLOCKBYTES pLockBytes, STATSTG FAR *
	pstatstg, DWORD grfStatFlag);
IOERR IOCreateStgFromBin (HIOFILE FAR *phFile);
