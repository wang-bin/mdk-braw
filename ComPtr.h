/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the mingw-w64 runtime package.
 * No warranty is given; refer to the file DISCLAIMER.PD within this package.
 */

#ifndef _WRL_CLIENT_H_
#define _WRL_CLIENT_H_

#include <stddef.h>
#if defined(_WIN32)
#include <unknwn.h>
/* #include <weakreference.h> */
#include <roapi.h>

/* #include <wrl/def.h> */
//#include <wrl/internal.h>
#endif
#if defined(__APPLE__)
#include <CoreFoundation/CFPlugInCOM.h>
#endif

#define __WRL_CLASSIC_COM__

namespace Microsoft {
    namespace WRL {
        namespace Details {
            template <typename T> class ComPtrRefBase {
            protected:
                T* ptr_;

            public:
                typedef typename T::InterfaceType InterfaceType;

#ifndef __WRL_CLASSIC_COM__
                operator IInspectable**() const  {
                    static_assert(__is_base_of(IInspectable, InterfaceType), "Invalid cast");
                    return reinterpret_cast<IInspectable**>(ptr_->ReleaseAndGetAddressOf());
                }
#endif

                operator IUnknown**() const {
                    static_assert(__is_base_of(IUnknown, InterfaceType), "Invalid cast");
                    return reinterpret_cast<IUnknown**>(ptr_->ReleaseAndGetAddressOf());
                }
            };

            template <typename T> class ComPtrRef : public Details::ComPtrRefBase<T> {
            public:
                ComPtrRef(T *ptr) {
                    ComPtrRefBase<T>::ptr_ = ptr;
                }

                operator void**() const {
                    return reinterpret_cast<void**>(ComPtrRefBase<T>::ptr_->ReleaseAndGetAddressOf());
                }

                operator T*() {
                    *ComPtrRefBase<T>::ptr_ = nullptr;
                    return ComPtrRefBase<T>::ptr_;
                }

                operator typename ComPtrRefBase<T>::InterfaceType**() {
                    return ComPtrRefBase<T>::ptr_->ReleaseAndGetAddressOf();
                }

                typename ComPtrRefBase<T>::InterfaceType *operator*() {
                    return ComPtrRefBase<T>::ptr_->Get();
                }

                typename ComPtrRefBase<T>::InterfaceType *const *GetAddressOf() const {
                    return ComPtrRefBase<T>::ptr_->GetAddressOf();
                }

                typename ComPtrRefBase<T>::InterfaceType **ReleaseAndGetAddressOf() {
                    return ComPtrRefBase<T>::ptr_->ReleaseAndGetAddressOf();
                }
            };

        }

        template<typename T> class ComPtr {
        public:
            typedef T InterfaceType;

            ComPtr() : ptr_(nullptr) {}
            ComPtr(decltype(nullptr)) : ptr_(nullptr) {}

            template<class U> ComPtr(U *other) : ptr_(other) {
                InternalAddRef();
            }

            ComPtr(const ComPtr &other) : ptr_(other.ptr_) {
                InternalAddRef();
            }

            template<class U>
            ComPtr(const ComPtr<U> &other) : ptr_(other.Get()) {
                InternalAddRef();
            }

            ComPtr(ComPtr &&other) : ptr_(nullptr) {
                if(this != reinterpret_cast<ComPtr*>(&reinterpret_cast<unsigned char&>(other)))
                    Swap(other);
            }

            template<class U>
            ComPtr(ComPtr<U>&& other) : ptr_(other.Detach()) {}

            ~ComPtr() {
                InternalRelease();
            }

            ComPtr &operator=(decltype(nullptr)) {
                InternalRelease();
                return *this;
            }

            ComPtr &operator=(InterfaceType *other) {
                if (ptr_ != other) {
                    InternalRelease();
                    ptr_ = other;
                    InternalAddRef();
                }
                return *this;
            }

            template<typename U>
            ComPtr &operator=(U *other)  {
                if (ptr_ != other) {
                    InternalRelease();
                    ptr_ = other;
                    InternalAddRef();
                }
                return *this;
            }

            ComPtr& operator=(const ComPtr &other) {
                if (ptr_ != other.ptr_)
                    ComPtr(other).Swap(*this);
                return *this;
            }

            template<class U>
            ComPtr &operator=(const ComPtr<U> &other) {
                ComPtr(other).Swap(*this);
                return *this;
            }

            ComPtr& operator=(ComPtr &&other) {
                ComPtr(other).Swap(*this);
                return *this;
            }

            template<class U>
            ComPtr& operator=(ComPtr<U> &&other) {
                ComPtr(other).Swap(*this);
                return *this;
            }

            void Swap(ComPtr &&r) {
                InterfaceType *tmp = ptr_;
                ptr_ = r.ptr_;
                r.ptr_ = tmp;
            }

            void Swap(ComPtr &r) {
                InterfaceType *tmp = ptr_;
                ptr_ = r.ptr_;
                r.ptr_ = tmp;
            }
/*
            operator Details::BoolType() const {
                return Get() != nullptr ? &Details::BoolStruct::Member : nullptr;
            }
*/
            operator bool() const { return !!Get(); }

            InterfaceType *Get() const  {
                return ptr_;
            }

            InterfaceType *operator->() const {
                return ptr_;
            }

            Details::ComPtrRef<ComPtr<T>> operator&()  {
                return Details::ComPtrRef<ComPtr<T>>(this);
            }

            const Details::ComPtrRef<const ComPtr<T>> operator&() const {
                return Details::ComPtrRef<const ComPtr<T>>(this);
            }

            InterfaceType *const *GetAddressOf() const {
                return &ptr_;
            }

            InterfaceType **GetAddressOf() {
                return &ptr_;
            }

            InterfaceType **ReleaseAndGetAddressOf() {
                InternalRelease();
                return &ptr_;
            }

            InterfaceType *Detach() {
                T* ptr = ptr_;
                ptr_ = nullptr;
                return ptr;
            }

            void Attach(InterfaceType *other) {
                if (ptr_ != other) {
                    InternalRelease();
                    ptr_ = other;
                }
            }

            unsigned long Reset() {
                return InternalRelease();
            }

            HRESULT CopyTo(InterfaceType **ptr) const {
                InternalAddRef();
                *ptr = ptr_;
                return S_OK;
            }

            HRESULT CopyTo(REFIID riid, void **ptr) const {
                return ptr_->QueryInterface(riid, ptr);
            }
#if (_WIN32 + 0)
            template<typename U>
            HRESULT CopyTo(U **ptr) const {
                return ptr_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(ptr));
            }

            template<typename U>
            HRESULT As(Details::ComPtrRef<ComPtr<U>> p) const {
                return ptr_->QueryInterface(__uuidof(U), p);
            }

            template<typename U>
            HRESULT As(ComPtr<U> *p) const {
                return ptr_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(p->ReleaseAndGetAddressOf()));
            }
#endif
            HRESULT AsIID(REFIID riid, ComPtr<IUnknown> *p) const {
                return ptr_->QueryInterface(riid, reinterpret_cast<void**>(p->ReleaseAndGetAddressOf()));
            }

            /*
            HRESULT AsWeak(WeakRef *pWeakRef) const {
                return ::Microsoft::WRL::AsWeak(ptr_, pWeakRef);
            }
            */
        protected:
            InterfaceType *ptr_;

            void InternalAddRef() const {
                if(ptr_)
                    ptr_->AddRef();
            }

            unsigned long InternalRelease() {
                InterfaceType *tmp = ptr_;
                if(!tmp)
                    return 0;
                ptr_ = nullptr;
                return tmp->Release();
            }
        };
    }
}

#endif
