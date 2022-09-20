#pragma once
#include <System/System.h>


#define AUNIQUECODE_GENERATE(return_type, method_name, ...)			\
	static inline const return_type* method_name () {				\
		static const return_type cl = { __VA_ARGS__ };				\
		return &cl;													\
	}


#define AUNIQUECODE_STRUCT(struct_name, return_type)				\
	struct struct_name: public IUniqueCode<return_type> {			\
		struct_name (const return_type code)						\
			: IUniqueCode(code) { }									\
	}


#define AUNIQUECODE_DOUBLE_STRUCT(struct_name, return_type)			\
	struct struct_name: public IUniqueCodeDouble<return_type> {		\
		struct_name (const uint32 code1, const uint32 code2)		\
			: IUniqueCodeDouble(code1, code2) { }					\
	}


#define AUNIQUECODE_STRUCT_U32(struct_name) AUNIQUECODE_STRUCT(struct_name, uint32)
#define AUNIQUECODE_STRUCT_U16(struct_name) AUNIQUECODE_STRUCT(struct_name, uint16)
#define AUNIQUECODE_STRUCT_U8(struct_name)  AUNIQUECODE_STRUCT(struct_name, uint8)

#define AUNIQUECODE_DOUBLE_STRUCT_U32(struct_name) AUNIQUECODE_DOUBLE_STRUCT(struct_name, uint32)
#define AUNIQUECODE_DOUBLE_STRUCT_U16(struct_name) AUNIQUECODE_DOUBLE_STRUCT(struct_name, uint16)
#define AUNIQUECODE_DOUBLE_STRUCT_U8(struct_name)  AUNIQUECODE_DOUBLE_STRUCT(struct_name, uint8)





template<typename CodeType>
struct IUniqueCode {
	IUniqueCode(const CodeType code = 0) : _code(code) { }

	const inline CodeType GetCode() const {
		return _code;
	}
private:
	const CodeType _code;
};


template<typename CodeType>
struct IUniqueCodeDouble {
	IUniqueCodeDouble(const CodeType code1 = 0, const CodeType code2 = 0) : _code1(code1), _code2(code2) { }

	const inline CodeType GetCode(const uint8 index) const {
		SystemAssert(index >= 1 && index <= 2);
		return index == 1 ? _code1 : _code2;
	}
private:
	const CodeType _code1;
	const CodeType _code2;
};


