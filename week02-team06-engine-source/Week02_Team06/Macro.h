#pragma once
#define DECLARE_CLASS( TClass, TSuperClass) \
	static UClass* GetPrivateStaticClass(); \
	public: \
		/** Typedef for the base class ({{ typedef-type }}) */ \
		typedef TSuperClass Super;\
		/** Typedef for {{ typedef-type }}. */ \
		typedef TClass ThisClass;\
		/** Returns a UClass object representing this class at runtime */ \
		inline static UClass* StaticClass() \
		{ \
			return GetPrivateStaticClass(); \
		} \
		virtual UClass* GetClass() const override \
		{ \
			return StaticClass(); \
		}
		
		
		//이 부분은 이미 할당한 메모리 풀에서 내용을 가져오는 것임
/** For internal use only; use StaticConstructObject() to create new objects. */ \
//inline void* operator new( const size_t InSize, EInternal* InMem ) \
		//{ \
		//	return (void*)InMem; \
		//} \
		///* Eliminate V1062 warning from PVS-Studio while keeping MSVC and Clang happy. */ \
		//inline void operator delete(void* InMem) \
		//{ \
		//	::operator delete(InMem); \
		//}

#define IMPLEMENT_CLASS(TClass, TSuperClass)\
    UClass* TClass::GetPrivateStaticClass()\
    {\
        static UClass ClassInfo = {\
            #TClass,\
            sizeof(TClass),\
            TSuperClass::StaticClass()\
        };\
        return &ClassInfo;\
    }

