// SampleObject.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <iostream>
#include <atlbase.h>
#include <atlcom.h>
#include <atlctl.h>
#include <windows.foundation.h>

using namespace ATL;

class MyModule : public ATL::CAtlExeModuleT<MyModule>
{
};

MyModule _module;

class CMyWinRTComponent : 
    public CComCoClass<CMyWinRTComponent>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public ABI::Windows::Foundation::IMemoryBuffer,
    public ABI::Windows::Foundation::IMemoryBufferReference
{
public:
    virtual ~CMyWinRTComponent() = default;

    // Because this object implements IMemoryBuffer and IMemoryBufferReference, both of which derive
    // from IInspectable, we specify that requests for IInspectable follow the path through one or
    // the other of their common ancestor. Doesn't matter which one, just has to be one of them.
    BEGIN_COM_MAP(CMyWinRTComponent)
        COM_INTERFACE_ENTRY(ABI::Windows::Foundation::IMemoryBuffer)
        COM_INTERFACE_ENTRY(ABI::Windows::Foundation::IMemoryBufferReference)
        COM_INTERFACE_ENTRY2(IInspectable, ABI::Windows::Foundation::IMemoryBufferReference)
    END_COM_MAP()

    STDMETHODIMP GetRuntimeClassName(
        /* [out] */ HSTRING* className) override
    {
        *className = nullptr;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetTrustLevel(
        /* [out] */ TrustLevel* trustLevel) override
    {
        *trustLevel = BaseTrust;
        return S_OK;
    }

    STDMETHODIMP GetIids(
        /* [out] */ ULONG* iidCount,
        /* [size_is][size_is][out] */ IID** iids) override
    {
        if (iidCount == nullptr || iids == nullptr)
            return E_POINTER;
        *iidCount = 2; // Number of interfaces
        *iids = static_cast<IID*>(CoTaskMemAlloc(*iidCount * sizeof(IID)));
        if (*iids == nullptr)
            return E_OUTOFMEMORY;
        (*iids)[0] = __uuidof(ABI::Windows::Foundation::IMemoryBuffer);
        (*iids)[1] = __uuidof(ABI::Windows::Foundation::IMemoryBufferReference);
        return S_OK;
    }

    STDMETHODIMP CreateReference(
        /* [out] */ ABI::Windows::Foundation::IMemoryBufferReference** value) override
    {
        AddRef();
        *value = this;
        return S_OK;
    }

    STDMETHODIMP get_Capacity(
        /* [out] */ UINT32* value) override
    {
        *value = 1024; // Example capacity
        return S_OK;
    }

    STDMETHODIMP add_Closed(
        /* [in] */ ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::Foundation::IMemoryBufferReference*, IInspectable*>* handler,
        /* [out] */ EventRegistrationToken* token) override
    {
        // For simplicity, we won't implement event handling in this example.
        return E_NOTIMPL;
    }

    STDMETHODIMP remove_Closed(
        /* [in] */ EventRegistrationToken token) override
    {
        // For simplicity, we won't implement event handling in this example.
        return E_NOTIMPL;
    }
};

int main()
{
    _module.InitializeCom();
    CComPtr<CMyWinRTComponent> component = new CComObject<CMyWinRTComponent>();

    CComPtr<ABI::Windows::Foundation::IMemoryBuffer> memoryBuffer;
    HRESULT hr = component->QueryInterface(IID_PPV_ARGS(&memoryBuffer));

    UINT32 capacity;
    CComPtr<ABI::Windows::Foundation::IMemoryBufferReference> memoryBufferReference;
    hr = component->QueryInterface(IID_PPV_ARGS(&memoryBufferReference));
    hr = memoryBufferReference->get_Capacity(&capacity);

    CComPtr<IInspectable> inspectable;
    TrustLevel tl;
    hr = component->QueryInterface(IID_PPV_ARGS(&inspectable));
    inspectable->GetTrustLevel(&tl);

    CComPtr<ABI::Windows::Foundation::IMemoryBufferReference> memoryBufferReference2;
    hr = inspectable->QueryInterface(IID_PPV_ARGS(&memoryBufferReference2));

    return 0;
}
