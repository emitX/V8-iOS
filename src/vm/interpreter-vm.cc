// Copyright 2013 Thai-Duong Nguyen. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "interpreter-vm.h"
#include "stdlib.h"
#include "string.h"
#include "math.h"
#include "stdio.h"
#include "assert.h"

#include "v8.h"
#include "runtime.h"
#include "debug.h"
#include "arguments.h"

#define _ARM_DEVICE_

#ifndef _WIN32
#include "inttypes.h"
#endif

namespace v8 {
namespace internal {


#define FREE free
#define MALLOC malloc


#define _ASSERT(X)
#define _INT128 __int128
#define _UINT_64 uint64_t
#define _INT_64  int64_t
    
#define _X87_STACK(X) mX87Stack[(X) % X87_STACK_SIZE]
#define _X87_STACK_STATUS(X) mX87StackStatus[(X) % X87_STACK_SIZE]
//#define NEED_CLEAR_FLAG
//#define KEEP_VM_FLAG

bool allowTrace = false;
//#define SHOW_CALL_TRACE
#define _CONDITION_TRACE if (_allowTrace) 

class builtin_data{
public:
	unsigned int data1;
	unsigned int data2;
};
const double _PI = 4.0*atan(1.0);
//opcode
typedef _UINT_64 (*builtin_func_2param)(unsigned int arg1, unsigned int arg2);
typedef unsigned int (*builtin_func_one_param)(unsigned int arg);
typedef int64_t (*SimulatorRuntimeCall)(uint32_t arg0,
                                        uint32_t arg1,
                                        uint32_t arg2,
                                        uint32_t arg3);

static unsigned char* lastIP;


#define _OUT_OF_MEMORY_EXCEPTION \
				*esp = MEM_GET_UINT32(m_handler_address);\
				while (MEM_GET_UINT32(*esp + 8))   \
				{									\
					*esp = MEM_GET_UINT32(*esp);   \
				}                                  \
				MEM_GET_UINT32(m_handler_address) = MEM_GET_UINT32(*esp); *esp += 4;\
				*esp += 4;                          \
				MEM_GET_UINT32(m_external_caught) = 0;\
				MEM_GET_UINT32(m_pending_exception) = 0x0F; \
				*esi = 0;                              \
				*ebp = MEM_GET_UINT32(*esp); *esp += 4; \
				*edx = MEM_GET_UINT32(*esp); *esp += 4; \
				ip = reinterpret_cast<unsigned char*>(MEM_GET_UINT32(*esp));\
				*esp += 4;							\
				return;				\

#define _TERMINATION_EXCEPTION_ \
					*esp = MEM_GET_UINT32(m_handler_address);\
					while (MEM_GET_UINT32(*esp + 8))\
					{								\
						*esp = MEM_GET_UINT32(*esp);\
					}								\
					MEM_GET_UINT32(m_handler_address) = MEM_GET_UINT32(*esp); *esp += 4;\
					*esi = 0;						\
					*ebp = MEM_GET_UINT32(*esp); *esp += 4;\
					*edx = MEM_GET_UINT32(*esp); *esp += 4;\
					ip = reinterpret_cast<unsigned char*>(MEM_GET_UINT32(*esp));\
					*esp += 4;							\
					return;				\



typedef struct {
	unsigned int eax;
	unsigned int ecx;
	unsigned int edx;
	unsigned int ebx;
	unsigned int esp;
	unsigned int ebp;
	unsigned int esi;
	unsigned int edi;
	unsigned int ip;
} CALL_STACK_INF;

static CALL_STACK_INF CallStack[100];
static int callStackPoint = -1;

void Interpreter::pushCallStack() {
	callStackPoint++;
	memccpy(&CallStack[callStackPoint], mRegisters, 1, sizeof(mRegisters));
	CallStack[callStackPoint].ip = (unsigned int)lastIP;
};
void Interpreter::popCallStack()
{
	callStackPoint--;
}
    
static Interpreter *interpret = NULL;

int Interpreter::execute_regexp_code(void* matcher_func,
                          void* input,
                          int start_offset,
						  void* input_start,
                          void* input_end,
                          void* output,
                          void* stack_base,
                          int direct_call)
{
		//init parameter		
	//save register;
	unsigned char* oldCode = mCode;
	unsigned char* oldIP = ip;
	mCode = (unsigned char*)matcher_func;
	ip = mCode;

	bool oldDirFlag = mDirectionFlag;

	unsigned int savedReg[8];
	memcpy(savedReg, mRegisters, sizeof(mRegisters));
	unsigned int oldFlags = mFlags;
	unsigned char *origin_stack = mStack;
	
	if (*esp != (unsigned int)mStack)
	{
		mStack = reinterpret_cast<unsigned char*>(*esp);
	}

	*reinterpret_cast<unsigned int*>(&mStack) &= -4; 

	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = (unsigned int)direct_call;	
	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = (unsigned int)stack_base;	
	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = (unsigned int)output;	
	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = (unsigned int)input_end;
	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = (unsigned int)input_start;
	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = (unsigned int)start_offset;
	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = (unsigned int)input;
	mStack -= 4;
	
	*reinterpret_cast<unsigned int*>(mStack) = 0xFFFFFFFF; //return
		
	mRegisters[ESP] = *reinterpret_cast<unsigned int*>(&mStack);
	executeOpCode();
	int ret = (int)mRegisters[EAX];

	//restore registers
	mFlags = oldFlags;
	memcpy(mRegisters, savedReg, sizeof(mRegisters));
	mStack = origin_stack;
	mCode = oldCode;
	ip = oldIP;
	mDirectionFlag = oldDirFlag;
	if (*esp != (unsigned int)mStack)
	{
		mStack = reinterpret_cast<unsigned char*>(*esp);
	}
	return ret;
}

void* Interpreter::execute_code(unsigned char* code, unsigned char* entry, void* function, void* receiver, int argc, void*** args)
{
	//init parameter		
	//save register;
	unsigned char* oldCode = mCode;
	unsigned char* oldIP = ip;
	mCode = code;
	ip = mCode;

	bool oldDirFlag = mDirectionFlag;

	unsigned int savedReg[8];
	memcpy(savedReg, mRegisters, sizeof(mRegisters));
	unsigned int oldFlags = mFlags;
	unsigned char *origin_stack = mStack;
	
	if (*esp != (unsigned int)mStack)
	{
		mStack = reinterpret_cast<unsigned char*>(*esp);
	}

	*reinterpret_cast<unsigned int*>(&mStack) &= -4; 
	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = (unsigned int)args;
	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = (unsigned int)argc;
	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = (unsigned int)receiver;
	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = (unsigned int)function;
	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = (unsigned int)entry;	
	mStack -= 4;
	*reinterpret_cast<unsigned int*>(mStack) = 0xFFFFFFFF; //return
		
	mRegisters[ESP] = *reinterpret_cast<unsigned int*>(&mStack);
	executeOpCode();
	void* ret = (void*)mRegisters[EAX];

	//restore registers
	mFlags = oldFlags;
	memcpy(mRegisters, savedReg, sizeof(mRegisters));
	mStack = origin_stack;
	mCode = oldCode;
	ip = oldIP;
	mDirectionFlag = oldDirFlag;
	if (*esp != (unsigned int)mStack)
	{
		mStack = reinterpret_cast<unsigned char*>(*esp);
	}
	return ret;
}
Interpreter::Interpreter(unsigned char *code, unsigned int entry, unsigned int stackSize)
{

	m_c_entry_fp_address = (unsigned int)ExternalReference(Top::k_c_entry_fp_address).address();
	m_context_address = (unsigned int)ExternalReference(Top::k_context_address).address();
    m_scope_depth = (unsigned int)ExternalReference::heap_always_allocate_scope_depth().address();
	m_handler_address = (unsigned int)ExternalReference(Top::k_handler_address).address();
	m_external_caught = (unsigned int)ExternalReference(Top::k_external_caught_exception_address).address();
	m_pending_exception = (unsigned int)ExternalReference(Top::k_pending_exception_address).address();
	m_runtime_gc_function = reinterpret_cast<unsigned int>(&Runtime::PerformGC);
	m_terminal_exception = (unsigned int)Factory::termination_exception().location();
	m_the_hole_value_location = (unsigned int)ExternalReference::the_hole_value_location().address();
	m_fp_address = (unsigned int) ExternalReference(Top::k_handler_address).address();

	mCode = code;
	//ip = reinterpret_cast<unsigned char*>(entry);
	memset(mRegisters, 0, sizeof(mRegisters));
	ip = mCode + entry;
	//init registers
	eax = &mRegisters[EAX];
	ebx = &mRegisters[EBX];
	ecx = &mRegisters[ECX];
	edx = &mRegisters[EDX];
	esp = &mRegisters[ESP];
	ebp = &mRegisters[EBP];
	esi = &mRegisters[ESI];
	edi = &mRegisters[EDI];

	AX = (register16_ptr)&mRegisters[EAX];
	BX = (register16_ptr)&mRegisters[EBX];
	CX = (register16_ptr)&mRegisters[ECX];
	DX = (register16_ptr)&mRegisters[EDX];

	SP = (register16_ptr)&mRegisters[ESP];
	BP = (register16_ptr)&mRegisters[EBP];
	SI = (register16_ptr)&mRegisters[ESI];
	DI = (register16_ptr)&mRegisters[EDI];


	AH = ((register8_ptr)AX) + 1;
	AL = ((register8_ptr)AX);
	BH = ((register8_ptr)BX) + 1;
	BL = ((register8_ptr)BX);
	CH = ((register8_ptr)CX) + 1;
	CL = ((register8_ptr)CX);
	DH = ((register8_ptr)DX) + 1;
	DL = ((register8_ptr)DX);

	mRegisters8[0] = AL;
	mRegisters8[1] = CL;
	mRegisters8[2] = DL;
	mRegisters8[3] = BL;
	mRegisters8[4] = AH;
	mRegisters8[5] = CH;
	mRegisters8[6] = DH;
	mRegisters8[7] = BH;

	memset(mRegisters, 0, NUMBER_OF_REGISTERS * sizeof (unsigned int));

	mStackSize = DEFAULT_STACK_SIZE;
	mStartStack = (unsigned char*)malloc(mStackSize);
	mStack = mStartStack + mStackSize - 64;

	mRegisters[ESP] = (unsigned int)mStack;

	mX87SP = X87_STACK_SIZE;
    memset(mX87StackStatus, 1, sizeof(mX87StackStatus));
	mFlags = 0;
}

Interpreter::~Interpreter()
{
	//DuongNT: must be fixed later
	free(mStartStack);
}

#define _PUSH(X) *esp -= 4; MEM_GET_UINT32(*esp) = (X);

//ENTER code_object, pf_addr, context_addr, argc
//            4          4           4        1
void Interpreter::_enterExitFrame()
{
	_PUSH(*ebp);
	*ebp = *esp;
	_PUSH(0);
	_PUSH(MEM_GET_UINT32(ip)); ip += 4; //code object
	MEM_GET_UINT32(ip) = *ebp; ip += 4; //Top::k_c_entry_fp_address
	MEM_GET_UINT32(ip) = *esi; ip += 4; //Top::k_context_address
	*edi = MEM_GET_UINT32(*eax); 
	//4 = StandardFrameConstants::kCallerSPOffset - kPointerSize;
	*esi = *ebp + (*eax << 2) + 4;
	*esp -= (*ip) << 2; ip ++;
	*esp &= -4;
}
//ENTER code_object, pf_addr, context_addr, stack_space, argc
//            4          4           4        4           1
void Interpreter::_enterAPIExitFrame()
{
	_PUSH(*ebp);
	*ebp = *esp;
	_PUSH(0);
	_PUSH(MEM_GET_UINT32(ip)); ip += 4; //code object
	MEM_GET_UINT32(ip) = *ebp; ip += 4; //Top::k_c_entry_fp_address
	MEM_GET_UINT32(ip) = *esi; ip += 4; //Top::k_context_address
	
	//4 = StandardFrameConstants::kCallerSPOffset - kPointerSize;
	*esi = *ebp + ((MEM_GET_UINT32(ip)) << 2) + 4; ip += 4;
	*esp -= (*ip) << 2; ip ++;
	*esp &= -4;
}

//LEAVE context_adr, fp_adr
void Interpreter::_leaveExitFrame()
{
	*ecx = MEM_GET_UINT32(*ebp + 4);
	*ebp = MEM_GET_UINT32(*ebp);
	*esp = *esi + 4;
	*esi = MEM_GET_UINT32(ip); ip += 4;
	_PUSH(*ecx);
	MEM_GET_UINT32(ip) = 0; ip += 4;

}


#define _NORMAL_EXCEPTION_				\
					*esp = MEM_GET_UINT32(m_fp_address);\
					printf("\n0x%x : esp: 0x%x\n", m_fp_address, *esp);\
					MEM_GET_UINT32(m_fp_address) = MEM_GET_UINT32(*esp); *esp += 4;\
					*ebp = MEM_GET_UINT32(*esp); *esp += 4;\
					*edx = MEM_GET_UINT32(*esp); *esp += 4;\
					*esi = 0;								\
					if ((*ebp) != 0) {								\
						*esi = MEM_GET_UINT32(*ebp - 4);	\
						printf("ebp != 0\n");\
					}\
					printf("ip: 0x%x\n", ip);\
					ip = reinterpret_cast<unsigned char*>(MEM_GET_UINT32(*esp));\
					printf("ip: 0x%x\n", ip);\
					*esp += 4;							\
					return;				\

  // eax: result parameter for PerformGC, if any
  // ebx: pointer to C function  (C callee-saved)
  // ebp: frame pointer  (restored after C call)
  // esp: stack pointer  (restored after C call)
  // edi: number of arguments including receiver  (C callee-saved)
  // esi: pointer to the first argument (C callee-saved)
//inc(Operand::StaticVariable(scope_depth));
//dec(Operand::StaticVariable(scope_depth));
#define _CSTUB_CORE_(_DO_GC_, _INC_SCOPE_, _DEC_SCOPE_)		\
		_DO_GC_												\
		_INC_SCOPE_											\
		MEM_GET_UINT32(*esp) = *edi;   					   \
		MEM_GET_UINT32(*esp + 4) = *esi;				   \
		_PUSH((unsigned int)ip);							\
        { \
            Arguments args(*edi, reinterpret_cast<Object**>(*esi)); \
            _args = args; \
            _UINT_64 ret_val = (reinterpret_cast<builtin_func_2param>(getExternalFunction(*ebx))(*edi, *esi)); \
			*eax = ret_val & 0xFFFFFFFF; \
			*edx = (ret_val >> 32) & 0xFFFFFFFF; \
        }\
		_DEC_SCOPE_												\
		if (((*eax)  & 0x03) == 0x03)								\
		{														\
			/*failed*/											\
			if ((*eax & 0x0C) == 0x0C) { /*out of memory*/		\
				_OUT_OF_MEMORY_EXCEPTION						\
			} else if ((*eax & 0x0C)){											\
				temp = MEM_GET_UINT32(m_pending_exception);\
				*eax = temp; \
				*edx =  MEM_GET_UINT32(m_the_hole_value_location);\
				MEM_GET_UINT32(m_pending_exception) = MEM_GET_UINT32(m_the_hole_value_location);\
				if (temp == m_terminal_exception)				\
				{												\
					_TERMINATION_EXCEPTION_						\
				} else { /*normal exception*/					\
					_NORMAL_EXCEPTION_							\
				}												\
			}													\
		} else {												\
			if (mode_ == ExitFrame::MODE_DEBUG) {				\
				for (int i = 0; i < 5; i++) {						\
					MEM_GET_UINT32(regs_debug[4 - i]) =  MEM_GET_UINT32(*ebp - 28 + i * 4);			\
				}													\
			}														\
			*ecx = MEM_GET_UINT32((*ebp) + 4);						\
			*ebp = MEM_GET_UINT32(*ebp);						\
			*esp = (*esi) + 4;									\
			*esi = MEM_GET_UINT32(m_context_address);					\
			MEM_GET_UINT32(m_context_address) = 0;					\
			/*_PUSH(*ecx);*/											\
			MEM_GET_UINT32(m_c_entry_fp_address) = 0;							\
			/*leave exit frame*/									\
			ip = reinterpret_cast<unsigned char*>(*ecx);				\
			return;												\
		}														\

void Interpreter::CStubCore() {
	
	//ip += 5; return;	
	//enter frame here
	unsigned int temp;
    int s = sizeof(temp);	
	unsigned int code_object = MEM_GET_UINT32(ip); ip += 4;	
    Arguments _args(0, NULL);

	unsigned char mode_ = *ip++;
	
	/*
	m_c_entry_fp_address = fp_address;
	m_context_address = context_address;
	m_handler_address = handler_adr;
	m_terminal_exception = termination_exception;
	m_pending_exception = pending_exception;
	*/	

	_PUSH(*ebp);
	*ebp = *esp;
	_PUSH(0);
	_PUSH(code_object);
	MEM_GET_UINT32(m_c_entry_fp_address) = *ebp;
	MEM_GET_UINT32(m_context_address) = *esi; 
	*edi = *eax; 
	//4 = StandardFrameConstants::kCallerSPOffset - kPointerSize;
	*esi = *ebp + ((*eax) << 2) + 4;
	unsigned int regs_debug[5];
	if (mode_ == ExitFrame::MODE_DEBUG) {
		for (int i = 0; i < 5; i++) {
			regs_debug[i] = MEM_GET_UINT32(ip); ip += 4;
			_PUSH(MEM_GET_UINT32(regs_debug[i]));
		}
	}

	*esp -= 2 * 4; 
	*esp &= -8;
	MEM_GET_UINT32((*ebp) - 4) = *esp;
	

#define _DO_GC ((builtin_func_one_param)(m_runtime_gc_function))(*eax);
#define _INC_SCOPE	MEM_GET_UINT32(m_scope_depth)++;
#define _DEC_SCOPE	MEM_GET_UINT32(m_scope_depth)--;

	_CSTUB_CORE_(,,);
	_CSTUB_CORE_(_DO_GC,,);		//retry first time
	 Failure* failure = Failure::InternalError();   
	 *eax = reinterpret_cast<unsigned int>(failure);
	_CSTUB_CORE_(_DO_GC,_INC_SCOPE,_DEC_SCOPE); //retry second time
}
void Interpreter::ArgumentTrampoline()
{
	if ((int)*eax >= (int)*ebx) {
		if (*ebx == 0xFFFFFFFF) { //do not need to check argument
			ip = reinterpret_cast<unsigned char*>(*edx);
			return;
		} else {
			_PUSH(*ebp);
			*ebp = *esp;
			_PUSH(0x0E); //StackFrame::ARGUMENTS_ADAPTOR - 7
			_PUSH(*edi);			
			_PUSH((*eax) << 1);
			*eax = *ebp + ((*eax) << 2) + 0x08;
			//*ecx = 0xFFFFFFFF;
			for (int i = *ebx; i >= 0; i--) {
				_PUSH(MEM_GET_UINT32(*eax));
				*eax -= 4;
			}
		}
	} else { //actual argument count < expected argument count

		_PUSH(*ebp);
		*ebp = *esp;
		_PUSH(0x0E); //StackFrame::ARGUMENTS_ADAPTOR - 7
		_PUSH(*edi);
		_PUSH((*eax) << 1);
		*edi = *ebp + ((*eax) << 2) + 0x08;
		for (int i = *eax; i >= 0; i--) {
			_PUSH(MEM_GET_UINT32(*edi));
			*edi -= 4;
		}
		
		unsigned int undefinedVal = MEM_GET_UINT32(ip + 1);
		for (int i = (*ebx) - (*eax); i > 0; i--) {
			_PUSH(undefinedVal);
		}
		*edi = MEM_GET_UINT32(*ebp - 8);
	}
	//*ecx = *ebx;
	_PUSH((unsigned int)ip);
	ip = reinterpret_cast<unsigned char*>(*edx);//call
}
void Interpreter::JSConstructCall()
{
	if ((*edi) & 0x01) {
		*ecx = MEM_GET_UINT32(*edi - 1);
		if (MEM_GET_UINT8(*ecx + 0x07) == 0xA6) {
			*ebx = MEM_GET_UINT32(*edi + 0x0F);
			*ebx = MEM_GET_UINT32(*ebx + 0x0B);
			*ebx += 0x1F;
			ip = reinterpret_cast<unsigned char*>(*ebx);
			return;
		}
	}

	MEM_GET_UINT32(*esp + ((*eax) << 2) + 4) = *edi;
	*ebx = 0;
	*edx = MEM_GET_UINT32(*esi + 0x17);
	*edx = MEM_GET_UINT32(*edx + 0x0b);
	*edi = MEM_GET_UINT32(*edx + 0x6f);
	*edx = MEM_GET_UINT32(*edx + 0xe7);
	*edx += 0x1F;
	ip += MEM_GET_INT32(ip) + 4;

}
void Interpreter::ExitArgumentTrampoline()
{
	*ebx = MEM_GET_UINT32(*ebp - 12);
	*esp = *ebp + 4;
	*ebp = MEM_GET_INT32(*ebp);
	*ecx = MEM_GET_UINT32(*esp); *esp += 4;
	*esp = *esp + ((*ebx) << 1) + 0x04;		
	ip = reinterpret_cast<unsigned char*>(*ecx);
}
unsigned int Interpreter::decodeSIB(unsigned char sib)
{
	unsigned char index = (sib >> 3) & 0x07;
	switch (index)
	{
	case 0x04: //100 none index
		return mRegisters[sib & 0x07];
	default:
		return mRegisters[sib & 0x07] + (mRegisters[index] << (sib >> 6));
	}
}
unsigned int Interpreter::decodeRM(unsigned char rm, int *size, bool *isAddr)
{
	*size = 0;
	unsigned char index;
	switch (rm & 0xC7) //11000111
	{
	case 0x04:	//100b [ESP] special case. [index * scale] + disp32
		*isAddr = true;
		index = ((*ip) >> 3) & 0x07;
		switch ((*ip) & 0x07)
		{
		case 0x05:
			*size = 5;
			if (index == 0x04)
				return MEM_GET_INT32(ip + 1);
			else			
				return (mRegisters[index] << ((*ip) >> 6)) + MEM_GET_INT32(ip + 1);
		default:
			*size = 1;
			return decodeSIB(*ip);
		}
		
	case 0x05:  //101b
		*size = 4;
		*isAddr = true;
		return MEM_GET_UINT32(ip);
	case 0x00: //[register]
	case 0x01: 
	case 0x02: 
	case 0x03: 
	case 0x06: 
	case 0x07: 
		*isAddr = true;
		return mRegisters[rm & 0x07];

	case 0x40 | 0x04:	//100b	[EBP + index * scale + disp8]
		*size = 2;
		*isAddr = true;
		
		index = ((*ip) >> 3) & 0x07;
		switch ((*ip) & 0x07)
		{
		case 0x05: //*
			if (index == 0x04) //esp
				return *ebp + (char)(*(ip + 1)); 
			else
				return *ebp + (mRegisters[index] << ((*ip) >> 6)) + (char)(*(ip + 1)); 		
		default:
			return decodeSIB(*ip) + (char)(*(ip + 1));		
		}
	case 0x40 | 0x05: //[register] + disp8 		
	case 0x40 | 0x00: 
	case 0x40 | 0x01: 
	case 0x40 | 0x02: 
	case 0x40 | 0x03: 
	case 0x40 | 0x06: 
	case 0x40 | 0x07: 
		*size = 1;
		*isAddr = true;

		return mRegisters[rm & 0x07] + (char)*ip;

	case 0x80 | 0x04:	//100b
		*size = 5;
		*isAddr = true;

		return decodeSIB(*ip) + (int)MEM_GET_UINT32(ip + 1);
	case 0x80 | 0x05: //[register] + disp32 		
	case 0x80 | 0x00: 
	case 0x80 | 0x01: 
	case 0x80 | 0x02: 
	case 0x80 | 0x03: 
	case 0x80 | 0x06: 
	case 0x80 | 0x07: 
		*size = 4;
		*isAddr = true;
		return mRegisters[rm & 0x07] + (int)MEM_GET_UINT32(ip);

	case 0xC0: //register value
	case 0xC1:
	case 0xC2:
	case 0xC3:
	case 0xC4:
	case 0xC5:
	case 0xC6:
	case 0xC7:
		*isAddr = false;
		return mRegisters[rm & 0x07];
	}
	
	assert(false);
	return -1;
}

unsigned char Interpreter::readUINT8(unsigned char rm)
{
	if ((rm & 0xC0) == 0xC0) //register mode
	{
		switch (rm & 0x07)
		{
		case 0x00:
			return *AL;
		case 0x01:
			return *CL;
		case 0x02:
			return *DL;
		case 0x03:
			return *BL;
		case 0x04:
			return *AH;
		case 0x05:
			return *CH;
		case 0x06:
			return *DH;
		case 0x07:
			return *BH;
		}
	} else if ((rm & 0xC7) == 0x05)
	{		
		unsigned int adr = MEM_GET_INT32(ip);
		ip += 4;
		return MEM_GET_UINT8(adr);
	} else {
		int size;
		bool isAddr;
		unsigned int val = decodeRM(rm, &size, &isAddr);
		ip += size;
		if (isAddr) return MEM_GET_UINT8(val);
		return (unsigned char)val;
	}
	return 0xFF;
}
unsigned char Interpreter::readUINT8_keepIP(unsigned char rm)
{
	if ((rm & 0xC0) == 0xC0) //register mode
	{
		switch (rm & 0x07)
		{
		case 0x00:
			return *AL;
		case 0x01:
			return *CL;
		case 0x02:
			return *DL;
		case 0x03:
			return *BL;
		case 0x04:
			return *AH;
		case 0x05:
			return *CH;
		case 0x06:
			return *DH;
		case 0x07:
			return *BH;
		}
	} else if ((rm & 0xC7) == 0x05)
	{	unsigned int adr = MEM_GET_INT32(ip);
		//ip += 4;
		return MEM_GET_UINT8(adr);
	} else {
		int size;
		bool isAddr;
		unsigned int val = decodeRM(rm, &size, &isAddr);
		if (isAddr) return MEM_GET_UINT8(val);
		return (unsigned char)val;
	}
}
unsigned short Interpreter::readUINT16(unsigned char rm)
{
	if ((rm & 0xC0) == 0xC0) //register mode
	{
		switch (rm & 0x07)
		{
		case 0x00:
			return *AX;
		case 0x01:
			return *CX;
		case 0x02:
			return *DX;
		case 0x03:
			return *BX;
		case 0x04:
			return *SP;
		case 0x05:
			return *BP;
		case 0x06:
			return *SI;
		case 0x07:
			return *DI;
		}
	} else if ((rm & 0xC7) == 0x05)
	{		
		unsigned int val = MEM_GET_UINT32(ip);
		ip += 4;
		return (unsigned short)(MEM_GET_UINT16(val) & 0xFFFF);
	} else {
		int size;
		bool isAddr;
		unsigned int val = decodeRM(rm, &size, &isAddr);
		ip += size;
		if (isAddr) return MEM_GET_UINT16(val);
		return (unsigned short)val;
	}
	return 0;
}
unsigned int Interpreter::readUINT32(unsigned char rm)
{

	if ((rm & 0xC0) == 0xC0) //register mode
	{
		return mRegisters[rm & 0x07];
	} else if ((rm & 0xC7) == 0x05)
	{		
		unsigned int val = MEM_GET_UINT32(ip);
		ip += 4;
		return MEM_GET_UINT32(val);
	} else {
		int size;
		bool isAddr;
		unsigned int val = decodeRM(rm, &size, &isAddr);
		ip += size;
		if (isAddr) return MEM_GET_UINT32(val);
		return (unsigned int)val;
	}
}
unsigned int Interpreter::readUINT32_keepIP(unsigned char rm)
{

	if ((rm & 0xC0) == 0xC0) //register mode
	{
		return mRegisters[rm & 0x07];
	} else if ((rm & 0xC7) == 0x05)
	{		
		unsigned int val = MEM_GET_UINT32(ip);				
		return MEM_GET_UINT32(val);
	} else {
		int size;
		bool isAddr;
		unsigned int val = decodeRM(rm, &size, &isAddr);
		if (isAddr) return MEM_GET_UINT32(val);
		return (unsigned int)val;
	}
}
void Interpreter::writeUINT8(unsigned char rm, unsigned char value)
{
	if ((rm & 0xC0) == 0xC0) //register mode
	{
		switch (rm & 0x07)
		{
		case 0x00:
			*AL = value;
			return;
		case 0x01:
			*CL = value;
			return;
		case 0x02:
			*DL = value;
			return;
		case 0x03:
			*BL = value;
			return;
		case 0x04:
			*AH = value;
			return;
		case 0x05:
			*CH = value;
			return;
		case 0x06:
			*DH = value;
			return;
		case 0x07:
			*BH = value;
			return;
		}
	} else if ((rm & 0xC7) == 0x05)
	{		
		unsigned int val = MEM_GET_UINT32(ip);		
		ip += 4;
		MEM_GET_UINT8(val) = value;
	}else {
		int size;
		bool isAddr;
		unsigned int addr = decodeRM(rm, &size, &isAddr);
		ip += size;
		MEM_GET_UINT8(addr) = value;
	}
}
void Interpreter::writeUINT16(unsigned char rm, unsigned short value)
{
	if ((rm & 0xC0) == 0xC0) //register mode
	{
		switch (rm & 0x07)
		{
		case 0x00:
			*AX = value;
			return;
		case 0x01:
			*CX = value;
			return;
		case 0x02:
			*DX = value;
			return;
		case 0x03:
			*BX = value;
			return;
		case 0x04:
			*SP = value;
			return;
		case 0x05:
			*BP = value;
			return;
		case 0x06:
			*SI = value;
			return;
		case 0x07:
			*DI = value;
			return;
		}
	} else {
		int size;
		bool isAddr;
		unsigned int addr = decodeRM(rm, &size, &isAddr);
		ip += size;
		MEM_GET_UINT16(addr) = value;
	}
}
void Interpreter::writeUINT32(unsigned char rm, unsigned int value)
{
	if ((rm & 0xC0) == 0xC0) //register mode
	{
		mRegisters[rm & 0x07] = value;
		return;
	} else if ((rm & 0xC7) == 0x05)
	{		
		unsigned int val = MEM_GET_UINT32(ip);		
		MEM_GET_UINT32(val) = value;
		ip+= 4;
	}else {
		int size;
		bool isAddr;
		unsigned int addr = decodeRM(rm, &size, &isAddr);
		ip += size;
		MEM_GET_UINT32(addr) = value;
	}
}
    
#ifdef IOS_SIMULATOR

    
#ifdef _ARM_DEVICE_
#define GET_CPU_FLAGS(flags)     \
    asm volatile( \
        "mrs r4, cpsr \n\t"     \
        "mov r5, 0xF0000000 \n\t"\
        "and r4, r5 \n\t"\
        "ldr r5, %0 \n\t"\
        "str r4, [r5] \n\t"\
    :                       \
    : "m"(flags)       \
    :"memory" \
    );                      \
    temp++;temp--;   \

#else
    
#define GET_CPU_FLAGS(flags)     \
                  asm( \
                    "pushf \n\t"     \
                  "movl %0, %%eax \n\t"      \
                  "pop (%%eax) \n\t"         \
                  :                       \
                  : "m"(flags)       \
                  );                      \

#endif

#define _INT_3 
    //asm("int $3");
    
#else
    

#define GET_CPU_FLAGS(flags) \
	__asm {					\
		__asm pushf			 \
		__asm mov eAX, flags  \
		__asm pop [eAX]		  \
	}          

#define _INT_3    
#endif

    
#define _SIGN_BIT8 0x80
#define _SIGN_BIT32 0x80000000
    
#define _SET_NZCP_FLAGS8(V) \
    mFlags |= ((V & _SIGN_BIT8)?_SF:0) | ((V & 0xFF)?0:_ZF) | ((V & 0x01)?0:_PF) | ((V & 0x100)?_CF:0);
    
#define _SET_NZP_FLAGS32(V) \
    mFlags |= ((V & _SIGN_BIT32)?_SF:0) | ((V )?0:_ZF) | ((V & 0x01)?0:_PF);
    
    
#define _SET_ADD_O_FLAGS8(left, right, ret)\
    mFlags |= (((~((left) ^ (right))) & ((left) ^ (ret)) & _SIGN_BIT8))?_OF:0;
    
#define _SET_SUB_O_FLAGS8(left, right, ret)\
    mFlags |= ((((left) ^ (right)) & ((left) ^ (ret)) & _SIGN_BIT8))?_OF:0;
    
#define _SET_ADD_O_FLAGS32(left, right, ret)\
    mFlags |= (((~((left) ^ (right))) & ((left) ^ (ret)) & _SIGN_BIT32))?_OF:0;
    
#define _SET_SUB_O_FLAGS32(left, right, ret)\
    mFlags |= (((left) ^ (right)) & ((left) ^ (ret)) & _SIGN_BIT32)?_OF:0;
    
#define _SET_ADD_C_FLAGS32(left, right) \
    mFlags |= ((unsigned int)(right) > ((unsigned int)(~(left))))?_CF:0;
    
#define _SET_SUB_C_FLAGS32(left, right) \
    mFlags |= (((unsigned int)right) > ((unsigned int)left))?_CF:0;
    
#define _CLEAR_ALU_FLAGS mFlags &= ~(_ZF | _OF | _SF | _CF | _PF);
    
#define ADD32_FLAGS(flags, opr1_ptr, opr2) \
    _INT_3 \
    temp3 = *opr1_ptr + opr2;				\
    _CLEAR_ALU_FLAGS		\
    _SET_NZP_FLAGS32(temp3)  \
    _SET_ADD_O_FLAGS32((*opr1_ptr), opr2, temp3) \
    _SET_ADD_C_FLAGS32((*opr1_ptr), opr2); \
    *opr1_ptr = temp3;
    
#define ADD8_FLAGS(flags, opr1_ptr, opr2) \
    operand1_ptr = opr1_ptr; \
    temp3 = (*opr1_ptr) + opr2;               \
    _CLEAR_ALU_FLAGS		\
    _SET_NZCP_FLAGS8(temp3)  \
    _SET_ADD_O_FLAGS8((*opr1_ptr), opr2, temp3) \
    *opr1_ptr = (unsigned char)temp3;
    
#define INC32_FLAGS(flags, opr1_ptr) \
    ADD32_FLAGS(flags, opr1_ptr, 1);
    
    
#define INC8_FLAGS(flags, opr1_ptr) \
	ADD8_FLAGS(flags, opr1_ptr, 1);
    
#define SUB32_FLAGS(flags, opr1_ptr, opr2) \
    operand1_ptr = opr1_ptr;							\
    temp3 = *opr1_ptr - opr2;				\
    _CLEAR_ALU_FLAGS		\
    _SET_NZP_FLAGS32(temp3)  \
    _SET_SUB_O_FLAGS32((*opr1_ptr), opr2, temp3) \
    _SET_SUB_C_FLAGS32((*opr1_ptr), opr2); \
    *opr1_ptr= temp3;

    
#define SUB8_FLAGS(flags, opr1_ptr, opr2) \
    operand1_ptr = opr1_ptr; \
    temp3 = (*opr1_ptr) - opr2;               \
    _CLEAR_ALU_FLAGS		\
    _SET_NZCP_FLAGS8(temp3)  \
    _SET_SUB_O_FLAGS8((*opr1_ptr), opr2, temp3) \
    *opr1_ptr = (unsigned char)temp3;
    
    
#define DEC32_FLAGS(flags, opr1_ptr) \
	SUB32_FLAGS(flags, opr1_ptr, 1)    

#define DEC8_FLAGS(flags, opr1_ptr) \
	SUB8_FLAGS(flags, opr1_ptr, 1);    
    
#define CMP32_FLAGS(flags, opr1, opr2) \
	temp3 = (opr1 - opr2);				\
	_CLEAR_ALU_FLAGS	\
	_SET_NZP_FLAGS32(temp3)  \
	_SET_SUB_O_FLAGS32(opr1, opr2, temp3) \
	_SET_SUB_C_FLAGS32(opr1, opr2);
	
    
#define CMP8_FLAGS(flags, opr1, opr2) \
	temp = (opr1 - opr2);				\
	_CLEAR_ALU_FLAGS		\
	_SET_NZCP_FLAGS8(temp)  \
	_SET_SUB_O_FLAGS8(opr1, opr2, temp)
    
    
#define AND32_FLAGS(flags, opr1_ptr, opr2) \
	operand1_ptr = opr1_ptr;							\
	*(unsigned int*)operand1_ptr &= opr2;				\
	_CLEAR_ALU_FLAGS			\
	_SET_NZP_FLAGS32(*(unsigned int*)operand1_ptr)			\

#define OR32_FLAGS(flags, opr1_ptr, opr2) \
	operand1_ptr = opr1_ptr;							\
	*(unsigned int*)operand1_ptr |= opr2;				\
	_CLEAR_ALU_FLAGS			\
	_SET_NZP_FLAGS32(*(unsigned int*)operand1_ptr)			\

    
#define XOR32_FLAGS(flags, opr1_ptr, opr2) \
	operand1_ptr = opr1_ptr;							\
	*(unsigned int*)operand1_ptr ^= opr2;				\
	_CLEAR_ALU_FLAGS			\
	_SET_NZP_FLAGS32(*(unsigned int*)operand1_ptr)			\

    
#define TEST32_FLAGS(flags, opr1, opr2) \
	_CLEAR_ALU_FLAGS			\
	_SET_NZP_FLAGS32((opr1) & (opr2))			\

    
#define TEST8_FLAGS(flags, opr1, opr2) \
	temp = (opr1) & (opr2);				\
	_CLEAR_ALU_FLAGS		\
	_SET_NZCP_FLAGS8(temp)
    
#define SAR32_FLAGS(flags, opr1_ptr, opr2) \
	operand1_ptr = opr1_ptr;							\
	*(int*)operand1_ptr >>= opr2;				\
	_CLEAR_ALU_FLAGS			\
	_SET_NZP_FLAGS32(*(int*)operand1_ptr)			\

#define SHL32_FLAGS(flags, opr1_ptr, opr2) \
	operand1_ptr = opr1_ptr;							\
	*(unsigned int*)operand1_ptr <<= opr2;				\
	_CLEAR_ALU_FLAGS			\
	_SET_NZP_FLAGS32(*(int*)operand1_ptr)			\

    
#define SHR32_FLAGS(flags, opr1_ptr, opr2) \
	operand1_ptr = opr1_ptr;							\
	*(unsigned int*)operand1_ptr >>= opr2;				\
	_CLEAR_ALU_FLAGS			\
	_SET_NZP_FLAGS32(*(int*)operand1_ptr)
    
#define NEG32_FLAGS(flags, opr1_ptr) \
	operand1_ptr = opr1_ptr;							\
	*(int*)operand1_ptr = -*(int*)operand1_ptr;				\
	_CLEAR_ALU_FLAGS			\
	_SET_NZP_FLAGS32(*(int*)operand1_ptr)
    
    
#define NOT32_FLAGS(flags, opr1_ptr) \
	operand1_ptr = opr1_ptr;							\
	*(int*)operand1_ptr = ~*(int*)operand1_ptr;				\
	_CLEAR_ALU_FLAGS			\
	_SET_NZP_FLAGS32(*(int*)operand1_ptr)
    

#ifdef SHOW_CALL_TRACE
char _spaces[10240];
int  callCount = 0;
#endif

void Interpreter::executeOpCode()
{    
#ifdef SHOW_CALL_TRACE
	callCount++;
	int _callCount = callCount;
	/*if (_callCount > 0x00000436) 
		allowTrace = true;*/
	bool _allowTrace = allowTrace;
	_spaces[0] = '\0';
	_CONDITION_TRACE
		printf("Start call: %x: %d\n", ip);
#endif

	register unsigned char* code = mCode;
	register unsigned char  rm;
	register unsigned int temp;
	register unsigned int temp2;
    register unsigned int temp3;
	mDirectionFlag = false;
	unsigned int* flags_ptr = &mFlags;
	void* operand1_ptr;
	unsigned char* oldIP;

	int size = 0;
	mFlags = 0;
	mFPFlags = 0;
	bool isAddr = false;
	bool isExit = false;

	while (!isExit)
	{		
        
		lastIP = ip;			
		switch (*ip++)
		{
		default:
			{
				assert(false);
			}
			break;
        case 0x100: //Extended instructions
            switch (*ip++) {
            case 0:
                break;
            };
            break;
		case 0x99: //cdq			
			*edx = ((*eax) & 0x80000000)?(unsigned int)-1 : 0;

			break;
		case 0xFB: //call builtin function
			this->callBuiltinFunction(NULL);
			if (0xFFFFFFFF == reinterpret_cast<unsigned int>(ip)) {
				isExit = true;
			}
#ifdef SHOW_CALL_TRACE
			_CONDITION_TRACE
			{
				printf("%sreturn from builtin: %x, esp: %x: %d\n", _spaces, (unsigned int)ip, *esp);				
				if (strlen(_spaces) > 0)
					_spaces[strlen(_spaces) - 1] = '\0';
			}
#endif	
			break;
		case 0x06: //add_nf reg,rm
			rm = *ip++;
			if ((rm & 0xC0) == 0xC0) //register mode
			{
				mRegisters[(rm >> 3) & 0x07] += mRegisters[rm & 0x07];

			} else {
				int size;
				bool isAddr;
				unsigned int addr = decodeRM(rm, &size, &isAddr);
				ip += size;			
				mRegisters[(rm >> 3) & 0x07] += MEM_GET_UINT32(addr);
			}
			break;
		case 0x07://add_nf
			rm = *ip++;
			oldIP = ip;
			temp = readUINT32(rm);
			temp2 = MEM_GET_UINT32(ip);					
			temp += temp2;
			ip = oldIP;
			writeUINT32(rm, temp);

			ip += 4;
			break;		
		case 0x08: //add_nf rm, imm8
			{
				rm = *ip++;
				oldIP = ip;
				temp = readUINT32(rm);														
				temp += (unsigned int)(int)((char)(*ip));
				ip = oldIP;
				writeUINT32(rm, temp);

				ip++;				
			}
			break;
		case 0x00: //sub_nf
			rm = *ip++;
			oldIP = ip;
			temp = readUINT32(rm);
			temp2 = MEM_GET_UINT32(ip);					
			//SUB32_FLAGS(flags_ptr, &temp, temp2);
			temp -= temp2;
			ip = oldIP;
			writeUINT32(rm, temp);

			ip += 4;
			break;		
		case 0x01: //add opr, reg
			{			
				rm = *ip++;	
				if ((rm & 0xC0) == 0xC0) //register mode
				{					
					ADD32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], mRegisters[(rm >> 3) & 0x07]);
				} else if ((rm & 0xC7) == 0x05) //direct mode
				{
					unsigned int val = MEM_GET_UINT32(ip);
					ip += 4;
					ADD32_FLAGS(flags_ptr, reinterpret_cast<unsigned int*>(val), mRegisters[(rm >> 3) & 0x07]);

				} else {
					int size;
					bool isAddr;
					unsigned int val = decodeRM(rm, &size, &isAddr);
					ip += size;
					ADD32_FLAGS(flags_ptr, reinterpret_cast<unsigned int*>(val), mRegisters[(rm >> 3) & 0x07]);
				}			
			}
			break;
			
		case 0xFA: //sub_nf rm, imm8
			{
				rm = *ip++;
				oldIP = ip;
				temp = readUINT32(rm);														
				temp -= (unsigned int)(int)((char)(*ip));
				ip = oldIP;
				writeUINT32(rm, temp);

				ip++;				
			}
			break;

		case 0x26: //sub_nf rm,reg
			rm = *ip++;
			if ((rm & 0xC0) == 0xC0) //register mode
			{				
				mRegisters[rm & 0x07]-= mRegisters[(rm >> 3) & 0x07];
			} else if ((rm & 0xC7) == 0x05) //direct mode
			{
				unsigned int val = MEM_GET_UINT32(ip);
				ip += 4;				
				*reinterpret_cast<unsigned int*>(val) -= mRegisters[(rm >> 3) & 0x07];
			} else {				
				unsigned int addr = decodeRM(rm, &size, &isAddr);
				ip += size;			
				*reinterpret_cast<unsigned int*>(addr) -= mRegisters[(rm >> 3) & 0x07];
			}
			break;
		case 0x27: //sub_nf reg,rm
			rm = *ip++;
			if ((rm & 0xC0) == 0xC0) //register mode
			{
				mRegisters[(rm >> 3) & 0x07] -= temp = mRegisters[rm & 0x07];
			} else if ((rm & 0xC7) == 0x05) //direct mode
			{
				unsigned int val = MEM_GET_UINT32(ip);
				ip += 4;				
				mRegisters[(rm >> 3) & 0x07] -= *reinterpret_cast<unsigned int*>(val);
			} else {
				int size;
				bool isAddr;
				unsigned int addr = decodeRM(rm, &size, &isAddr);
				ip += size;			
				mRegisters[(rm >> 3) & 0x07] -= MEM_GET_UINT32(addr);
			}
			break;

		case 0x9E: //sahf            
			mFlags = (mFlags & 0xFFFFFF00) | *AH;
			break;
		case 0x04: //add AL, imm8
			ADD8_FLAGS(flags_ptr, AL, *ip);
			ip++;
			break;
		case 0xC9://leave
			mRegisters[ESP] = mRegisters[EBP] + 4;
			mRegisters[EBP] = MEM_GET_INT32(mRegisters[EBP]);
			break;
		case 0x05: //add eax,imm32
			ADD32_FLAGS(flags_ptr, eax, MEM_GET_UINT32(ip));
			ip += 4;
			break;
		case 0x03: //add reg32, rm32
			{
				rm = *ip++;				
				operand1_ptr = &mRegisters[(rm >> 3) & 0x07];
				temp2 = readUINT32(rm);
				ADD32_FLAGS(flags_ptr, ((unsigned int*)operand1_ptr), temp2);
			}
			break;
		case 0x25: //and eax, imm		 			
			AND32_FLAGS(flags_ptr, eax, MEM_GET_UINT32(ip));
			ip += 4;
			break;
		case 0x23: //and reg32, rm32
			{
				rm = *ip++;								 
				temp2 = readUINT32(rm);
				AND32_FLAGS(flags_ptr, &mRegisters[(rm >> 3) & 0x07], temp2);
			}
			break;
		case 0x21: //and rm32,reg
			{
				rm = *ip++;
				oldIP = ip;
				temp = readUINT32(rm);
				AND32_FLAGS(flags_ptr, &temp, mRegisters[(rm >> 3) & 0x07]);
				ip = oldIP;
				writeUINT32(rm, temp);
				
			}
			break;
		case 0xCC://int 3
			{				
				//do nothing
			}
			break;
		case 0x90://NOP
			break;
		case 0x48: //dec EAX;
			DEC32_FLAGS(flags_ptr, eax);
			break;
		case 0x49: //dec ECX;			
			DEC32_FLAGS(flags_ptr, ecx);
			break;
		case 0x4A: //dec EDX;
			DEC32_FLAGS(flags_ptr, edx);
			break;
		case 0x4B: //dec EBX;
			DEC32_FLAGS(flags_ptr, ebx);
			break;
		case 0x4C: //dec ESP;
			DEC32_FLAGS(flags_ptr, esp);
			break;
		case 0x4D: //dec EBP;
			DEC32_FLAGS(flags_ptr, ebp);
			break;
		case 0x4E: //dec ESI;
			DEC32_FLAGS(flags_ptr, esi);			
			break;
		case 0x4F: //dec EDI;
			DEC32_FLAGS(flags_ptr, edi);
			break;
		case 0xF7:
			{
				rm = *ip++;
				switch (rm & 0xF8)
				{
				case 0x00:
				case 0x40:
				case 0x80:
				case 0xC0://test
					if ((rm & 0xC0) == 0xC0) //register mode
					{
						TEST32_FLAGS(flags_ptr, mRegisters[rm & 0x07], MEM_GET_UINT32(ip));
						ip += 4;
					} else if ((rm & 0xC7) == 0x05)
					{					
						TEST32_FLAGS(flags_ptr, MEM_GET_UINT32(MEM_GET_UINT32(ip)), MEM_GET_UINT32(ip + 4));
						ip += 8;			
					} else {
						int size;
						bool isAddr;
						unsigned int val = decodeRM(rm, &size, &isAddr);
						ip += size;
						TEST32_FLAGS(flags_ptr, MEM_GET_UINT32(val), MEM_GET_UINT32(ip));						
						ip += 4;
					}
					break;
				case 0xE0: //mul eax, 
					{
						_UINT_64 result = mRegisters[EAX] * mRegisters[rm & 0x07];
						mRegisters[EAX] = result & 0xFFFFFFFF;
						mRegisters[EDX] = (result >> 32) & 0xFFFFFFFF;
					}
					break;
				case 0xD8: //neg reg32
					NEG32_FLAGS(flags_ptr, &mRegisters[rm & 0x07])
					break;
				case 0xD0: //not reg32
					NOT32_FLAGS(flags_ptr, &mRegisters[rm & 0x07])
					break;
				case 0xE8: //imul
					{
						_INT_64 result = *reinterpret_cast<int*>(&mRegisters[EAX]) * *reinterpret_cast<int*>(&mRegisters[rm & 0x07]);
						*reinterpret_cast<int*>(&mRegisters[EAX]) = result & 0xFFFFFFFF;
						*reinterpret_cast<int*>(&mRegisters[EDX]) = (result >> 32) & 0xFFFFFFFF;
					}
					break;
				case 0xF8: //idiv
					{
						unsigned int val[2] = {mRegisters[EAX], mRegisters[EDX]};
						_INT_64 x = *reinterpret_cast<_INT_64*>(val);
						mRegisters[EAX] = (unsigned int)(x / mRegisters[rm & 0x07]);
						mRegisters[EDX]= (unsigned int)(x % mRegisters[rm & 0x07]);

					}
					break;
				default:
					assert(false);
				}
			}
			break;
		case 0xE9: //jump
			{
				temp = (unsigned int)ip;				
				ip += MEM_GET_INT32(ip) + 4;
#ifdef SHOW_CALL_TRACE
				_CONDITION_TRACE
				{

					printf("%s->jump: %x -> %x, esp: %x\n", _spaces, lastIP, (unsigned int)ip, *esp);
				}
#endif					
			}
			break;
		case 0xEB: //jump near
			{
				temp = (unsigned int)ip;
				ip += (char)MEM_GET_UINT8(ip) + 1;
#ifdef SHOW_CALL_TRACE
				_CONDITION_TRACE
				{

					printf("%s->jump: %x -> %x, esp: %x\n", _spaces, lastIP, (unsigned int)ip, *esp);
				}
#endif	
			}
			break;
		case 0x8D: //lea
			{
				rm = *ip++;

				mRegisters[(rm >> 3) & 0x07] = decodeRM(rm, &size, &isAddr);
				ip += size;
			}
			break;
		case 0xC6: //mov_b rm,imm8
			{
			
				rm = *ip++;
				if (((rm >> 3) & 0x07) != 0)
				{
					rm = rm;
					assert(false);
				}
				if ((rm & 0xC0) == 0xC0) //register mode
				{
					switch (rm & 0x07)
					{
					case 0x00:
						*AL = *ip++;
						break;
					case 0x01:
						*CL = *ip++;
						break;
					case 0x02:
						*DL = *ip++;
						break;
					case 0x03:
						*BL = *ip++;
						break;
					case 0x04:
						break;
						*AH = *ip++;
					case 0x05:
						*CH = *ip++;
						break;
					case 0x06:
						*DH = *ip++;
						break;
					case 0x07:
						*BH = *ip++;
						break;
					}
				} else {			
					unsigned int addr = decodeRM(rm, &size, &isAddr);
					ip += size;
					MEM_GET_UINT8(addr) = *ip++;
				}
			}
			break;
		case 0x88: //mov_b rm,reg
			rm = *ip++;
			writeUINT8(rm, *mRegisters8[(rm >> 3) & 0x07]);
			break;
		case 0x8A: //mov_b reg,rm
			rm = *ip++;
			*mRegisters8[(rm >> 3) & 0x07] = readUINT8(rm);
			break;
		case 0xC7: //mov rm,imm32
			{
				rm = *ip++;
				if ((rm & 0xC0) == 0xC0) //register mode
				{			
					mRegisters[rm & 0x07] = MEM_GET_UINT32(ip);
					
					ip += 4;				
				} else {			
					unsigned int addr = decodeRM(rm, &size, &isAddr);
					ip += size;
					MEM_GET_UINT32(addr) = MEM_GET_UINT32(ip);
					ip += 4;
				}
			}
			break;
		case 0x89: //mov rm,reg
			{
				rm = *ip++;			
				writeUINT32(rm, mRegisters[(rm >> 3) & 0x07]);		

			}
			break;
		case 0xB8: //mov EAX, imm32
			mRegisters[EAX] = MEM_GET_UINT32(ip);		
			ip += 4;
			break;
		case 0xB9: //mov ECX, imm32
			mRegisters[ECX] = MEM_GET_UINT32(ip);
			ip += 4;
			break;
		case 0xBA: //mov EDX, imm32
			mRegisters[EDX] = MEM_GET_UINT32(ip);
			ip += 4;
			break;
		case 0xBB: //mov EBX, imm32
			mRegisters[EBX] = MEM_GET_UINT32(ip);
			ip += 4;
			break;
		case 0xBC: //mov ESP, imm32
			mRegisters[ESP] = MEM_GET_UINT32(ip);
			ip += 4;
			break;
		case 0xBD: //mov EBP, imm32
			mRegisters[EBP] = MEM_GET_UINT32(ip);
			ip += 4;
			break;
		case 0xBE: //mov ESI, imm32
			mRegisters[ESI] = MEM_GET_UINT32(ip);
			ip += 4;
			break;
		case 0xBF: //mov EDI, imm32
			mRegisters[EDI] = MEM_GET_UINT32(ip);
			ip += 4;
			break;
		case 0x8B: //mov REG, RM
			rm = *ip++;			
			mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
			break;
		case 0x6B://imul reg,reg,imm8
			rm = *ip++;		
			//*reinterpret_cast<int*>(&mRegisters[(rm >> 3) & 0x07]) = (int)mRegisters[rm & 0x07] * (char)*ip++;
			{
				_INT_64 val = (_INT_64)mRegisters[rm & 0x07] * (_INT_64)(char)(*ip++);
				if (((val >> 32) & 0xFFFFFFFF) < 0xFFFFFFFF) {
					mFlags |= _OF | _CF;
				} else mFlags &= ~(_OF | _CF);
				*reinterpret_cast<int*>(&mRegisters[(rm >> 3) & 0x07]) = val & 0xFFFFFFFF;
			}
			break;
		case 0x69://imul reg,reg,imm32
			rm = *ip++;		
			//*reinterpret_cast<int*>(&mRegisters[(rm >> 3) & 0x07]) = (int)mRegisters[rm & 0x07] * (int)MEM_GET_UINT32(ip);
			{
				_INT_64 val = (_INT_64)mRegisters[rm & 0x07] * (_INT_64)MEM_GET_UINT32(ip);
				if (((val >> 32) & 0xFFFFFFFF) < 0xFFFFFFFF) {
					mFlags |= _OF | _CF;
				} else mFlags &= ~(_OF | _CF);
				*reinterpret_cast<int*>(&mRegisters[(rm >> 3) & 0x07]) = val & 0xFFFFFFFF;
			}
			ip += 4;
			break;
		case 0x66: //virtual 8086
			{
				switch (*ip++)
				{
				case 0x89: //mov_w rm,reg
					rm = *ip++;
					writeUINT16(rm, mRegisters[(rm >> 3) & 0x07] & 0xFFFF);
					break;
				case 0x8B:  //mov_w reg, rm
					rm = *ip++;
					MEM_GET_UINT16(&mRegisters[(rm >> 3) & 0x07]) = readUINT16(rm);
					break;
				case 0x0F:
					switch (*ip++)
					{
					case 0x6E: //movd
						rm = *ip++;
						mXMMRegisters[(rm >> 3) & 0x07] = (int)readUINT32(rm);
						break;
					}

					break;
				}
			}
			break;
		case 0xFE: //dec b 8			
			{
				rm = *ip++;			
				unsigned char *oldIP = ip;
				unsigned char temp1 = readUINT8(rm);				
				//operand1_ptr = mRegisters8[(*ip) & 0x07];
				DEC8_FLAGS(flags_ptr, &temp1);
				ip = oldIP;
				writeUINT8(rm, temp1);				
			}
			break;
		case 0xFF:
			{
				rm = *ip++;
				switch ((rm >> 3) & 0x07)
				{
				case 0x04: //esp  - jmp
					temp = (unsigned int)ip;
					ip = reinterpret_cast<unsigned char*>(readUINT32(rm));			
#ifdef SHOW_CALL_TRACE
					_CONDITION_TRACE
					{
					
					printf("%s->jump: %x -> %x, esp: %x\n", _spaces, lastIP, (unsigned int)ip, *esp);
					}
#endif								
					break;
				case 0x06: //push 32
					temp = readUINT32(rm);
					mRegisters[ESP] -= 4;
					MEM_GET_UINT32(mRegisters[ESP]) = temp;
					//printf("push esp: %x, start: %x\n", *esp, mStartStack);
					break;
				case 0x02: //edx - call			

#ifndef USE_REDIRECTION
					if ((rm & 0xC0) == 0xC0 && ((rm & 0x07) == EBX || (rm & 0x07) == EAX)) //call builtin function
					{
						//const char *name = FunctionForAddr(*ebx);
#ifdef SHOW_CALL_TRACE			
						_CONDITION_TRACE
						printf("call builtin: %x, esp: %x\n", *ebx, *esp);
#endif
	
						if ((rm & 0x07) == EAX) {
							rm = rm;
						}
						mRegisters[ESP] -= 4; //push return address;
						MEM_GET_UINT32(mRegisters[ESP]) = reinterpret_cast<unsigned int>(ip);
						__int64 ret = reinterpret_cast<SimulatorRuntimeCall>(mRegisters[rm & 0x07])(MEM_GET_UINT32(mRegisters[ESP] + 4),
																	 MEM_GET_UINT32(mRegisters[ESP] + 8),
																	 MEM_GET_UINT32(mRegisters[ESP] + 12),
																	 MEM_GET_UINT32(mRegisters[ESP] + 16));

						*eax = ret & 0xFFFFFFFF;
						*edx = (ret >> 32) & 0xFFFFFFFF;

						ip = reinterpret_cast<unsigned char*>(MEM_GET_UINT32(mRegisters[ESP]));
						mRegisters[ESP] += 4;
						break;
					} /**/
#endif
					//pushCallStack();					
					temp = readUINT32(rm); //infact newIP
					mRegisters[ESP] -= 4; //push return address;
					MEM_GET_UINT32(mRegisters[ESP]) = reinterpret_cast<unsigned int>(ip);					
					ip = reinterpret_cast<unsigned char*>(temp); //go to function address
#ifdef SHOW_CALL_TRACE
					_CONDITION_TRACE
					{
					sprintf(_spaces, "%s ", _spaces);
					printf("%scall: %x -> %x, esp: %x: %d\n", _spaces, lastIP, (unsigned int)ip, *esp);
					}
#endif					
					break;
				case 0x00: //eax - inc rm8
					if ((rm & 0xC0) == 0xC0) //register mode
					{
						operand1_ptr = mRegisters8[rm & 0x07];
						INC8_FLAGS(flags_ptr, ((unsigned char*)operand1_ptr));
					} else {
						int size;
						bool isAddr;
						unsigned int val = decodeRM(rm, &size, &isAddr);
						ip += size;
						INC8_FLAGS(flags_ptr, (unsigned char*)val);
						
					}
					break;
				case 0x01: //ecx - dec rm
					if ((rm & 0xC0) == 0xC0) //register mode
					{					
						operand1_ptr = &mRegisters[rm & 0x07];
						DEC32_FLAGS(flags_ptr, ((unsigned int*)operand1_ptr));
						break;
					} else {
						int size;
						bool isAddr;
						unsigned int val = decodeRM(rm, &size, &isAddr);
						ip += size;						
						//MEM_GET_UINT32(val) = sub32_Flags(MEM_GET_UINT32(val), 1);		
						DEC32_FLAGS(flags_ptr, reinterpret_cast<unsigned int*>(val));
					}
					break;
				}

			}
			break;
		case 0xE8: //call
			mRegisters[ESP] -= 4; //push return address;
			MEM_GET_UINT32(mRegisters[ESP]) = reinterpret_cast<unsigned int>(ip) + 4;			
			ip += (int)MEM_GET_UINT32(ip) + 4;
			
#ifdef SHOW_CALL_TRACE
			_CONDITION_TRACE
			{			
			sprintf(_spaces, "%s ", _spaces);
			printf("%scall: %x -> %x, esp: %x: %d\n", _spaces, lastIP, (unsigned int)ip, *esp);
			}
#endif
			break;
		case 0xEA:
			temp = (unsigned int)ip + MEM_GET_INT32(ip) + 4;
#ifdef SHOW_CALL_TRACE
			_CONDITION_TRACE
			printf("call builtin: %x, esp: %x\n", temp, *esp);
#endif		
			{
				mRegisters[ESP] -= 4; //push return address;
				MEM_GET_UINT32(mRegisters[ESP]) = reinterpret_cast<unsigned int>(ip) + 4;
				_INT_64 ret = reinterpret_cast<SimulatorRuntimeCall>(temp)(MEM_GET_UINT32(mRegisters[ESP] + 4),
																	 MEM_GET_UINT32(mRegisters[ESP] + 8),
																	 MEM_GET_UINT32(mRegisters[ESP] + 12),
																	 MEM_GET_UINT32(mRegisters[ESP] + 16));
				*eax = ret & 0xFFFFFFFF;
				*edx = (ret >> 32) & 0xFFFFFFFF;
			}
			ip += 4;
			//mRegisters[ESP] += 12;
			break;
		case 0xCD: //call
			temp = (unsigned int)ip + MEM_GET_INT32(ip) + 4;

#ifdef SHOW_CALL_TRACE
			_CONDITION_TRACE			
			printf("call builtin: %x, esp: %x\n", temp, *esp);
#endif		
			{
				mRegisters[ESP] -= 4; //push return address;
				MEM_GET_UINT32(mRegisters[ESP]) = reinterpret_cast<unsigned int>(ip) + 4;
				unsigned int ret_adr = reinterpret_cast<unsigned int>(ip) + 4;
				_INT_64 ret = reinterpret_cast<SimulatorRuntimeCall>(temp)(MEM_GET_UINT32(mRegisters[ESP] + 4),
																	 MEM_GET_UINT32(mRegisters[ESP] + 8),
																	 MEM_GET_UINT32(mRegisters[ESP] + 12),
																	 MEM_GET_UINT32(mRegisters[ESP] + 16));
				*eax = ret & 0xFFFFFFFF;
				*edx = (ret >> 32) & 0xFFFFFFFF;
				ip = reinterpret_cast<unsigned char*>(ret_adr);
				mRegisters[ESP] += 4;
			}						
			break;
		case 0xCE://cstub
			CStubCore();
			if ((unsigned int)ip == 0xFFFFFFFF)
				isExit = true;
#ifdef SHOW_CALL_TRACE
			_CONDITION_TRACE
			{
                printf("%sreturn: %x, esp: %x\n", _spaces, (unsigned int)ip, *esp);
                _spaces[strlen(_spaces) - 1] = '\0';
			}
#endif
			break;
		case 0xCF:
			ArgumentTrampoline();
			break;
		case 0xD0:
			ExitArgumentTrampoline();
#ifdef SHOW_CALL_TRACE
			_CONDITION_TRACE
			{
            printf("%sreturn: %x, esp: %x\n", _spaces, (unsigned int)ip, *esp);
            _spaces[strlen(_spaces) - 1] = '\0';
			}
#endif
			break;
		case 0xD2:
			JSConstructCall();
#ifdef SHOW_CALL_TRACE
			_CONDITION_TRACE
			{
            printf("%sreturn: %x, esp: %x\n", _spaces, (unsigned int)ip, *esp);
            _spaces[strlen(_spaces) - 1] = '\0';
			}
#endif
			break;
			//conditional jump
		case 0x70://overflow
			if (mFlags & _OF)
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x71://no overflow
			if (!(mFlags & _OF))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x72: //below
			if ((mFlags & F_BELOW))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x73://not below (>= )
			if (!(mFlags & F_BELOW))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x74://equal
			if (mFlags & F_EQUAL)
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x75://not equal
			if (!(mFlags & F_EQUAL))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x76://F_BELOW_OR_EQUAL

			if (mFlags & (F_BELOW | F_EQUAL))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x77://above
			if (!(mFlags & (F_BELOW | F_EQUAL)))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x78:
			if ((mFlags & F_SIGN))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x79:
			if (!(mFlags & F_SIGN))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x7A:
			if (mFlags & F_PARITY)
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x7B:
			if (!(mFlags & F_PARITY))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x7C://less
			temp = mFlags & F_SIGN;
			temp2 = mFlags & _OF;
			if ((!temp && temp2) || (temp && !temp2))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x7D:// >=
			temp = mFlags & F_SIGN;
			temp2 = mFlags & _OF;
			if ((temp && temp2) || !(temp || temp2))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x7E:// <= 
			temp = mFlags & F_SIGN;
			temp2 = mFlags & _OF;
			if ((!temp && temp2) || (temp && !temp2) || (mFlags & _ZF))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;
		case 0x7F: //>
			temp = mFlags & F_SIGN;
			temp2 = mFlags & _OF;
			if (((temp && temp2) || !(temp || temp2)) && !(mFlags & _ZF))
				ip += (char)MEM_GET_UINT8(ip);
			ip++;
			break;

			//------
		case 0x58: //pop EAX
			mRegisters[EAX] = MEM_GET_UINT32(mRegisters[ESP]);
			mRegisters[ESP] += 4;
			break;
		case 0x59: //pop ECX
			mRegisters[ECX] = MEM_GET_UINT32(mRegisters[ESP]);
			mRegisters[ESP] += 4;
			break;
		case 0x5A: //pop EDX
			mRegisters[EDX] = MEM_GET_UINT32(mRegisters[ESP]);
			mRegisters[ESP] += 4;
			break;
		case 0x5B: //pop EBX
			mRegisters[EBX] = MEM_GET_UINT32(mRegisters[ESP]);
			mRegisters[ESP] += 4;
			break;
		case 0x5C: //pop ESP
			mRegisters[ESP] = MEM_GET_UINT32(mRegisters[ESP]);
			break;
		case 0x5D: //pop EBP
			mRegisters[EBP] = MEM_GET_UINT32(mRegisters[ESP]);
			mRegisters[ESP] += 4;
			break;
		case 0x5E: //pop ESI
			mRegisters[ESI] = MEM_GET_UINT32(mRegisters[ESP]);
			mRegisters[ESP] += 4;
			break;
		case 0x5F: //pop EDI		
			mRegisters[EDI] = MEM_GET_UINT32(mRegisters[ESP]);
			mRegisters[ESP] += 4;
			break;		
		case 0x8F: //pop 
			writeUINT32(*ip++, MEM_GET_UINT32(mRegisters[ESP]));
			mRegisters[ESP] += 4;
			break;
		case 0x61: //popad
			temp = (unsigned int)(mRegisters[ESP]);
			mRegisters[EDI] = MEM_GET_UINT32(temp);
			temp += 4;		
			mRegisters[ESI] = MEM_GET_UINT32(temp);
			temp += 4;
			mRegisters[EBP] = MEM_GET_UINT32(temp);
			temp += 8; //ignore ESP				
			mRegisters[EBX] = MEM_GET_UINT32(temp);
			temp += 4;
			mRegisters[EDX] = MEM_GET_UINT32(temp);
			temp += 4;
			mRegisters[ECX] = MEM_GET_UINT32(temp);
			temp += 4;
			mRegisters[EAX] = MEM_GET_UINT32(temp);

			mRegisters[ESP] += (8 << 2); //8 * 4
			break;
		case 0x9D: //popf
			mFlags = MEM_GET_UINT32(mRegisters[ESP]);
			mDirectionFlag = (mFlags & _DF);
			
			mRegisters[ESP] += 4;
			break;
		case 0x6A: //push imm8. push with sign
			mRegisters[ESP] -= 4;
			MEM_GET_UINT32(mRegisters[ESP]) = (char)*ip++;
			break;
		case 0x68: //push imm32
			mRegisters[ESP] -= 4;
			MEM_GET_UINT32(mRegisters[ESP]) = MEM_GET_UINT32(ip);
			//printf("push imm esp: %x, start: %x\n", *esp, mStartStack);
			ip += 4;			
			break;
		case 0x50: //push EAX
			mRegisters[ESP] -= 4;
			MEM_GET_UINT32(mRegisters[ESP]) = mRegisters[EAX];			
			break;
		case 0x51: //push ECX
			mRegisters[ESP] -= 4;
			MEM_GET_UINT32(mRegisters[ESP]) = mRegisters[ECX];			
			break;
		case 0x52: //push EDX
			mRegisters[ESP] -= 4;
			MEM_GET_UINT32(mRegisters[ESP]) = mRegisters[EDX];			
			break;
		case 0x53: //push EBX
			mRegisters[ESP] -= 4;
			MEM_GET_UINT32(mRegisters[ESP]) = mRegisters[EBX];			
			break;
		case 0x54: //push ESP
			MEM_GET_UINT32(mRegisters[ESP] - 4) = mRegisters[ESP];			
			mRegisters[ESP] -= 4;		
			break;
		case 0x55: //push EBP
			mRegisters[ESP] -= 4;
			MEM_GET_UINT32(mRegisters[ESP]) = mRegisters[EBP];			
			break;
		case 0x56: //push ESI
			mRegisters[ESP] -= 4;
			MEM_GET_UINT32(mRegisters[ESP]) = mRegisters[ESI];			
			break;
		case 0x57: //push EDI
			mRegisters[ESP] -= 4;
			MEM_GET_UINT32(mRegisters[ESP]) = mRegisters[EDI];			
			break;
		case 0x60: //pushA
			temp = mRegisters[ESP];			
			temp -= 4;
			MEM_GET_UINT32(temp) = mRegisters[EAX];
			temp -= 4;
			MEM_GET_UINT32(temp) = mRegisters[ECX];
			temp -= 4;
			MEM_GET_UINT32(temp) = mRegisters[EDX];
			temp -= 4;
			MEM_GET_UINT32(temp) = mRegisters[EBX];
			temp -= 4;
			MEM_GET_UINT32(temp) = mRegisters[ESP];
			temp -= 4;
			MEM_GET_UINT32(temp) = mRegisters[EBP];
			temp -= 4;
			MEM_GET_UINT32(temp) = mRegisters[ESI];
			temp -= 4;
			MEM_GET_UINT32(temp) = mRegisters[EDI];
			mRegisters[ESP] = temp;//(8 << 2);
			break;
		case 0x9C: //pushF
			mRegisters[ESP] -= 4;
			if (mDirectionFlag) mFlags |= _DF;
			else mFlags &= ~_DF;
			MEM_GET_UINT32(mRegisters[ESP]) = mFlags;
			break;
		case 0x0D: //or EAX, imm32
			OR32_FLAGS(flags_ptr, &mRegisters[EAX], MEM_GET_UINT32(ip));
			ip += 4;
			break;
		case 0x09: //or rm,reg
			rm = *ip++;
			if ((rm & 0xC0) == 0xC0) //register mode
			{
				OR32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], mRegisters[(rm >> 3) & 0x07]);				
			} else {
				int size;
				bool isAddr;
				unsigned int addr = decodeRM(rm, &size, &isAddr);
				ip += size;			
				OR32_FLAGS(flags_ptr, reinterpret_cast<unsigned int*>(addr), mRegisters[(rm >> 3) & 0x07]);
			}
			break;
		case 0xDA: //fisub_s
			rm = *ip++;
			switch (rm)
			{
			case 0xE9: //fucompp
                //mFPFlags &= ~(F_PARITY << 8);
                mFPFlags = 0;
                if (_X87_STACK_STATUS(mX87SP) || _X87_STACK_STATUS(mX87SP + 1) || isnan(_X87_STACK(mX87SP)) || isnan(_X87_STACK(mX87SP + 1))) {
                    mFPFlags |= ((_ZF | _CF | F_PARITY) << 8);
                } else if (_X87_STACK(mX87SP) < _X87_STACK(mX87SP + 1))
					mFPFlags |= (F_BELOW << 8);
				else {
					mFPFlags &= ~(F_BELOW << 8);
					if (_X87_STACK(mX87SP) == _X87_STACK(mX87SP + 1))
						mFPFlags |= (_ZF << 8);
					else mFPFlags &= ~(_ZF << 8);
				}         
                _X87_STACK_STATUS(mX87SP) = true;
                _X87_STACK_STATUS(mX87SP + 1) = true;
				mX87SP += 2;
			}
			break;
		case 0x0B: //or reg, rm
			rm = *ip++;
			if ((rm & 0xC0) == 0xC0) //register mode
			{
				OR32_FLAGS(flags_ptr, &mRegisters[(rm >> 3) & 0x07], mRegisters[rm & 0x07]);
			} else {			
				unsigned int addr = decodeRM(rm, &size, &isAddr);
				ip += size;				
				OR32_FLAGS(flags_ptr, &mRegisters[(rm >> 3) & 0x07], MEM_GET_UINT32(addr));
			}
			break;
		case 0xDC:
			rm = *ip++;
			switch (rm & 0xF8)
			{
			case 0xC0: //fadd
				_X87_STACK(mX87SP + (rm & 0x7)) += _X87_STACK(mX87SP);
				break;
			case 0xF8: //fdiv
				_X87_STACK(mX87SP + (rm & 0x7)) /= _X87_STACK(mX87SP);
				break;
			case 0xC8: //fmul
				_X87_STACK(mX87SP + (rm & 0x7)) *= _X87_STACK(mX87SP);
				break;
			case 0xE8: //fsub
				_X87_STACK(mX87SP + (rm & 0x7)) -= _X87_STACK(mX87SP);
				break;
			}
			break;
		case 0xDD: 
			rm = *ip++;
			switch (rm & 0xF8)
			{
			case 0xC0: //ffree
				_X87_STACK(mX87SP + (rm & 0x7)) = 0;
                _X87_STACK_STATUS(mX87SP + (rm & 0x7)) = true;
				break;
			case 0x48:
			case 0x88:
			case 0xC8:
			case 0x08: //fisttp_d
				{
					_INT_64 val = (_INT_64)_X87_STACK(mX87SP);
                    _X87_STACK_STATUS(mX87SP++) = false;
					//writeUINT32(rm, MEM_GET_UINT32(&val));
					if ((rm & 0xC0) == 0xC0) //register mode
					{
						mRegisters[rm & 0x07] = (unsigned int)(val & 0xFFFFFFFF);
						assert(false);
					} else if ((rm & 0x38) == 0x38){ //fstp_s
						if ((rm & 0xC0) == 0xC0) //register mode
						{
							assert(false);
						} else {
							int size;
							bool isAddr;
							unsigned int addr = decodeRM(rm, &size, &isAddr);
							ip += size;
							*reinterpret_cast<double*>(addr) = _X87_STACK(mX87SP);
                            _X87_STACK_STATUS(mX87SP++) = true;
						}
					} else if ((rm & 0x38) == 0x30) { //edx - fst_d
						if ((rm & 0xC0) == 0xC0) //register mode
						{
							assert(false);
						} else {
							int size;
							bool isAddr;
							unsigned int addr = decodeRM(rm, &size, &isAddr);
							ip += size;
							*reinterpret_cast<double*>(addr) = _X87_STACK(mX87SP);
                            _X87_STACK_STATUS(mX87SP) = false;
						}
					} else {
						int size;
						bool isAddr;
						unsigned int addr = decodeRM(rm, &size, &isAddr);
						ip += size;
						MEM_GET_UINT32(addr) = val & 0xFFFFFFFF;
						MEM_GET_UINT32(addr + 4) = (val >> 32) & 0xFFFFFFFF;
					}
				}
				break;
			case 0xD8:
				_X87_STACK(mX87SP + (rm & 0x07)) = _X87_STACK(mX87SP);
                _X87_STACK_STATUS(mX87SP + (rm & 0x07)) = false;
				_X87_STACK_STATUS(mX87SP) = true;
                mX87SP++;
				break;
			default:
				switch ((rm >> 3) & 0x07)
				{
				case 0:			//fld_d
					mX87SP--;
					temp = decodeRM(rm, &size, &isAddr);
                    assert(isAddr);
					ip += size;
					_X87_STACK(mX87SP) = *reinterpret_cast<double*>(temp);
                    _X87_STACK_STATUS(mX87SP) = false;
                    //printf("top 87 stack: %f\n", mX87Stack[mX87SP]);
					break;
				case 2:			//fstp
					_X87_STACK(mX87SP + (rm & 0x07)) = _X87_STACK(mX87SP);
                    _X87_STACK_STATUS(mX87SP + (rm & 0x07)) = false;
                    _X87_STACK_STATUS(mX87SP) = true;
					mX87SP++;
					break;
				case 3:			//fstp_d					
					temp = decodeRM(rm, &size, &isAddr);
                    assert(isAddr);
					ip += size;
					*reinterpret_cast<double*>(temp) = _X87_STACK(mX87SP);
                    //printf("---fstp_d: %f\n", *reinterpret_cast<double*>(temp));
                    _X87_STACK_STATUS(mX87SP) = true;
					mX87SP++;
					break;
				}				
			}
			break;
		case 0x9B: //wait

			break;
		case 0xDB: 
			{
				rm = *ip++;
				switch (rm & 0xF8)
				{
				case 0xE2://clear flag - FNCLEX
					//mFPFlags &= 0xFFFFFF00;
                    mFPFlags = 0;
					break;
				case 0xE8: //fucomi								
					//only check SP(0) is NaN or not
					if (isnan(_X87_STACK(mX87SP)) || _X87_STACK_STATUS(mX87SP))
						mFlags |= _PF;
					else mFlags &= ~_PF;										
					break;
				default: 
					if ((rm & 0x38) == 0x00) //eax - fild_s
					{					
						unsigned int v = readUINT32(rm);
						_X87_STACK(--mX87SP) = *reinterpret_cast<int*>(&v);
                        _X87_STACK_STATUS(mX87SP) = false;
					} else if ((rm & 0x38) == 0x10) //edx - fist_s
					{	
						int v = (int)_X87_STACK(mX87SP);
						writeUINT32(rm, MEM_GET_INT32(&v));
					} else {

						//fistp_s
						int val = (int)_X87_STACK(mX87SP++); //get and pop data
						writeUINT32(rm, MEM_GET_UINT32(&val));
						break;
					}
				}
			}
			break;
		case 0xDE: 
			rm = *ip++;
			switch (rm & 0xF8)
			{
			case 0xC0: //faddp
                if (isnan(_X87_STACK(mX87SP + (rm & 0x7))) || isnan(_X87_STACK(mX87SP))
                    || _X87_STACK_STATUS(mX87SP + (rm & 0x7)) || _X87_STACK_STATUS(mX87SP)) {
                    //_X87_STACK(mX87SP + (rm & 0x7)) = 
                    _X87_STACK_STATUS(mX87SP + (rm & 0x7)) = true;
                    
                } else {
				    _X87_STACK(mX87SP + (rm & 0x7)) += _X87_STACK(mX87SP);                    
                }
                _X87_STACK_STATUS(mX87SP) = true;
				mX87SP++;
				break;
			case 0xF8:
				_X87_STACK(mX87SP + (rm & 0x7)) /= _X87_STACK(mX87SP);
                _X87_STACK_STATUS(mX87SP) = true;
				mX87SP++;
				break;
			case 0xC8: //fmul
                //printf("mX87SP: %d - %d \n",  mX87SP, (rm & 0x7));
                //printf("mX87SP: %f - %f \n",  mX87Stack[mX87SP + (rm & 0x7)], mX87Stack[mX87SP]);
				_X87_STACK(mX87SP + (rm & 0x7)) *= _X87_STACK(mX87SP);                
                _X87_STACK_STATUS(mX87SP) = true;
				mX87SP++;
                //printf("mX87SP: %f \n", mX87Stack[mX87SP]);
				break;
			case 0xE8: //fsub
				_X87_STACK(mX87SP + (rm & 0x7)) -= _X87_STACK(mX87SP);                
                _X87_STACK_STATUS(mX87SP) = true;
				mX87SP++;
				break;
			case 0xE0: //fsubrp
				_X87_STACK(mX87SP + (rm & 0x7)) = _X87_STACK(mX87SP) - _X87_STACK(mX87SP +(rm & 0x7));
                _X87_STACK_STATUS(mX87SP) = true;
				mX87SP++;
				break;
			}		
			break;
		case 0xDF:
			rm = *ip++;
			switch (rm)
			{
			case 0xE0: //fnstsw_ax
                *AX = mFPFlags & 0xFFFF;
				break;
			case 0xE9: //fucomip
				assert(false);
				break;
			default:
				if ((rm & 0x38) == 0x28) //fild_d
				{
					if ((rm & 0xC0) == 0xC0) //register mode
					{
						_ASSERT(false);
					} else {
						int size;
						bool isAddr;
						unsigned int val = decodeRM(rm, &size, &isAddr);
						ip += size;
						mX87SP--;					
						_X87_STACK(mX87SP) = (double)*reinterpret_cast<_INT_64*>(val);
                        _X87_STACK_STATUS(mX87SP) = false;
                        
					}
				}
			}
			break;
		case 0xD9: //Co-processor
			rm = *ip++;
			switch (rm)
			{

			case 0xE4: //ftst
				assert(false);//maybe need to implement this instruction in the future
				break;
			case 0xE1: //fabs
				if (_X87_STACK(mX87SP) < 0)
					_X87_STACK(mX87SP) = -_X87_STACK(mX87SP);
				break;									
			case 0xE0: //fchs
				_X87_STACK(mX87SP) = -_X87_STACK(mX87SP);
				break;
			case 0xFF: //fcos
				_X87_STACK(mX87SP) = cos(_X87_STACK(mX87SP));
				break;
			case 0xF7:
				mX87SP++;
			case 0xF5: //fprem1
				{
					_INT_64 v1 = (_INT_64)_X87_STACK(mX87SP);
					_INT_64 v2 = (_INT_64)_X87_STACK(mX87SP + 1);
					_X87_STACK(mX87SP)= (double)(v1 % v2);
				}
				break;
			case 0xFC: //frndint
				_X87_STACK(mX87SP) = floor(_X87_STACK(mX87SP) + 0.5f);
				break;
			case 0xFE://fsin
				_X87_STACK(mX87SP) = sin(_X87_STACK(mX87SP));
				break;
			case 0xEB://fldpi
				_X87_STACK(--mX87SP) = _PI;
                _X87_STACK_STATUS(mX87SP) = false;
				break;
			case 0xE8: //fld1
				_X87_STACK(--mX87SP) = 1.0f;
                _X87_STACK_STATUS(mX87SP) = false;
				break;
			case 0xEE: //fldz
				_X87_STACK(--mX87SP) = 0.0f;
                _X87_STACK_STATUS(mX87SP) = false;
				break;
			default:
				if ((rm & 0xF8) == 0xC0) //fld
				{
					mX87SP--;
					_X87_STACK(mX87SP) = _X87_STACK(mX87SP + (rm & 0x07) + 1);
                    _X87_STACK_STATUS(mX87SP) = false;                    
				} else if ((rm & 0x38) == 0x38){ //fstp_s
					_X87_STACK_STATUS(mX87SP) = true;
                    float v = (float)_X87_STACK(mX87SP++); //load & pop
					writeUINT32(rm, MEM_GET_INT32(&v));
				} else {//fld_s
					mX87SP--;
					_X87_STACK(mX87SP) = (int)readUINT32(rm);
                    _X87_STACK_STATUS(mX87SP) = false;
				}
			}
			break;
	
		case 0xF2:
			switch (*ip++)
			{
			case 0x0F:
				switch  (*ip++)
				{
				default: //notice
					assert(false);
					break;
				case 0x2C: //cvttsd2si
					rm = *ip++;
					if ((rm & 0xC0) == 0xC0) //register mode
					{
						_ASSERT(false);
					} else {
						int size;
						bool isAddr;
						unsigned int val = decodeRM(rm, &size, &isAddr);
						ip += size;
						mRegisters[(rm >> 3) & 0x07] = (int)*reinterpret_cast<double*>(val);	
					}
					break;
				}
				break;
			case 0x5E:
				rm = *ip++;
				mXMMRegisters[(rm >> 3) & 0x07] /= mXMMRegisters[rm & 0x07];
				break;
			case 0x11: //movsd
				temp = temp;
				break;
			}

			break;
		case 0xF3:
			switch (*ip++)
			{

			default:
				assert(false);
				break;
			case 0xA5: //rep movs
				for (;*ecx > 0; (*ecx)--)
				{
					MEM_GET_UINT32(mRegisters[EDI]) = MEM_GET_UINT32(mRegisters[ESI]);
					if (mDirectionFlag)
					{
						mRegisters[ESI] -= 4;
						mRegisters[EDI] -= 4;
					} else {
						mRegisters[ESI] += 4;
						mRegisters[EDI] += 4;
					}
				}
				break;
			case 0xAB: //rep stos
				for (;*ecx > 0; (*ecx)--)
				{
					MEM_GET_UINT32(mRegisters[EDI]) = mRegisters[EAX];	
					if (mDirectionFlag)
						mRegisters[EDI] -= 4;
					else
						mRegisters[EDI] += 4;
				}			
				break;
			case 0x0F:
				switch (*ip++)
				{
				default:
					assert(false);
					break;
				case 0x2C: //cvttss2si  
					{
						rm = *ip++;
						unsigned int v = readUINT32(rm);				
						mRegisters[(rm >> 3) & 0x07] = (int)*reinterpret_cast<float*>(&v);
					}
					break;
				case 0x5A: //cvtss2sd
					rm = *ip++;
					mXMMRegisters[(rm >> 3) & 0x07] = mXMMRegisters[rm & 0x07];
					break;
				}
				break;
			}
			break;
		case 0xC3: //RET 			
			ip = reinterpret_cast<unsigned char*>(MEM_GET_UINT32(mRegisters[ESP]));
			mRegisters[ESP] += 4;
			
			if ((unsigned int)ip == 0xFFFFFFFF)
				isExit = true;			
#ifdef SHOW_CALL_TRACE
			_CONDITION_TRACE
			{
				printf("%sreturn: %x -> %x, esp: %x: %d\n", _spaces, lastIP, (unsigned int)ip, *esp);
				if (strlen(_spaces) > 0)
					_spaces[strlen(_spaces) - 1] = '\0';
			}
#endif		
			break;
		case 0xC2: //RET  16
			temp = MEM_GET_UINT32(mRegisters[ESP]);
			mRegisters[ESP] += 4 + MEM_GET_UINT16(ip);
			ip = reinterpret_cast<unsigned char*>(temp);
			
			if ((unsigned int)ip == 0xFFFFFFFF)
				isExit = true;		
#ifdef SHOW_CALL_TRACE
			_CONDITION_TRACE
			{

				printf("%sreturn: %x -> %x, esp: %x: %\n", _spaces, lastIP, (unsigned int)ip, *esp);
				_spaces[strlen(_spaces) - 1] = '\0';
			}
#endif
			break;

		case 0xAB://stos [edi] = eax
			MEM_GET_UINT32(*edi) = *eax;
			if (mDirectionFlag)
				*edi -= 4;
			else *edi += 4;
			break;
		case 0x2D: //sub eax, imm32
			SUB32_FLAGS(flags_ptr, eax, MEM_GET_UINT32(ip));
			ip += 4;
			break;
		case 0x29: //sub rm,reg
			rm = *ip++;
			if ((rm & 0xC0) == 0xC0) //register mode
			{				
				operand1_ptr = &mRegisters[rm & 0x07];
				temp = mRegisters[(rm >> 3) & 0x07];
				SUB32_FLAGS(flags_ptr, ((unsigned int*)operand1_ptr), temp);
			} else if ((rm & 0xC7) == 0x05) //direct mode
			{
				unsigned int val = MEM_GET_UINT32(ip);
				ip += 4;				
				*reinterpret_cast<unsigned int*>(val) -= mRegisters[(rm >> 3) & 0x07];
			} else {				
				unsigned int addr = decodeRM(rm, &size, &isAddr);
				ip += size;
				temp = mRegisters[(rm >> 3) & 0x07];
				SUB32_FLAGS(flags_ptr, reinterpret_cast<unsigned int*>(addr), temp);
			}
			break;
		case 0x2B: //sub reg,rm
			rm = *ip++;
			if ((rm & 0xC0) == 0xC0) //register mode
			{
				operand1_ptr = &mRegisters[(rm >> 3) & 0x07];
				temp = mRegisters[rm & 0x07];
				SUB32_FLAGS(flags_ptr, ((unsigned int*)operand1_ptr), temp);
			}  else if ((rm & 0xC7) == 0x05) //direct mode
			{
				unsigned int val = MEM_GET_UINT32(ip);
				ip += 4;				
				operand1_ptr = &mRegisters[(rm >> 3) & 0x07];
				SUB32_FLAGS(flags_ptr,  ((unsigned int*)operand1_ptr), MEM_GET_UINT32(val));
			} else {
				int size;
				bool isAddr;
				unsigned int addr = decodeRM(rm, &size, &isAddr);
				ip += size;
				operand1_ptr = &mRegisters[(rm >> 3) & 0x07];
				SUB32_FLAGS(flags_ptr,  ((unsigned int*)operand1_ptr), MEM_GET_UINT32(addr));
			}
			break;
		case 0x2C: //sub al,imm8			 
			DEC8_FLAGS(flags_ptr, AL);
			break;
		case 0x80: 
			rm = *ip++;
			switch (rm  & 0x38)
			{
			case 0x05 << 3: //ebp - sub reg8, rm8
				{
					oldIP = ip;
					unsigned char tmp = readUINT8(rm);
					SUB8_FLAGS(flags_ptr, &temp, *ip);
					ip = oldIP;
					writeUINT8(rm, tmp);
				}
				break;
			case 7 << 3: //cmp rm, imm8
				temp = readUINT8(rm);
				temp2 = MEM_GET_UINT8(ip++);

				CMP8_FLAGS(flags_ptr, (unsigned char)temp, (unsigned char)temp2);
				break;
			default:
				assert(false);
			}
			break;
		case 0x2A: //subb reg, rm
			rm = *ip++;
			if ((rm & 0xC0) == 0xC0) //register mode
			{
				operand1_ptr = mRegisters8[(rm >> 3) & 0x07];
				SUB8_FLAGS(flags_ptr, ((unsigned char*)operand1_ptr), *mRegisters8[rm & 0x07]);
				
			} else if ((rm & 0xC7) == 0x05) //direct mode
			{
				unsigned int val = MEM_GET_UINT32(ip);
				ip += 4;				
				operand1_ptr = mRegisters8[(rm >> 3) & 0x07];
				SUB8_FLAGS(flags_ptr, ((unsigned char*)operand1_ptr), MEM_GET_UINT8(val));
			} else {
				int size;
				bool isAddr;
				unsigned int val = decodeRM(rm, &size, &isAddr);
				ip += size;			
				operand1_ptr = mRegisters8[(rm >> 3) & 0x07];
				SUB8_FLAGS(flags_ptr, ((unsigned char*)operand1_ptr), MEM_GET_UINT8(val));
				
			}

			break;
		case 0x84: //test_b rm8,reg8
			rm = *ip++;
			if ((rm & 0xC0) == 0xC0) //register mode
			{
				TEST8_FLAGS(flags_ptr, *mRegisters8[(rm >> 3) & 0x07], *mRegisters8[rm & 0x07]);
				
			} else {
				int size;
				bool isAddr;
				unsigned int val = decodeRM(rm, &size, &isAddr);
				ip += size;			
				TEST8_FLAGS(flags_ptr, MEM_GET_UINT8(val), *mRegisters8[(rm >> 3) & 0x07]);					
			}
			break;
		case 0xF6: //test_b reg, imm8
			
			rm = *ip++;
			{
				unsigned char tmp = readUINT8(rm);
				TEST8_FLAGS(flags_ptr, tmp, *ip);
				ip++;
			}
			
			break;

		case 0xA9://test EAX, imm32
			TEST32_FLAGS(flags_ptr, mRegisters[EAX], MEM_GET_UINT32(ip));
				
			ip += 4;
			break;
		case 0xA8: //test al, imm8
			TEST8_FLAGS(flags_ptr, *AL, *ip);
			ip++;
				
			break;
		case 0x85: //test rm32, reg32
			rm = *ip++;
			if ((rm & 0xC0) == 0xC0) //register mode
			{
				TEST32_FLAGS(flags_ptr, mRegisters[rm & 0x07], mRegisters[(rm >> 3) & 0x07]);
				
			} else {
				int size;
				bool isAddr;
				unsigned int val = decodeRM(rm, &size, &isAddr);
				ip += size;
				TEST32_FLAGS(flags_ptr, MEM_GET_UINT32(val), mRegisters[(rm >> 3) & 0x07]);
					
			}
			break;

		case 0x91://exchange EAX, ECX
			temp = *eax;
			*eax = *ecx;
			*ecx = temp;
			break;
		case 0x92://exchange EAX, EDX
			temp = *eax;
			*eax = *edx;
			*edx = temp;
			break;
		case 0x93://exchange EAX, EBX
			temp = *eax;
			*eax = *ebx;
			*ebx = temp;
			break;
		case 0x94://exchange EAX, ESP
			temp = *eax;
			*eax = *esp;
			*esp = temp;
			break;
		case 0x95://exchange EAX, EBP
			temp = *eax;
			*eax = *ebp;
			*ebp = temp;
			break;
		case 0x96://exchange EAX, ESI
			temp = *eax;
			*eax = *esi;
			*esi = temp;
			break;
		case 0x97://exchange EAX, EDI
			temp = *eax;
			*eax = *edi;
			*edi = temp;
			break;	
		case 0x87://exchange reg, reg
			rm = *ip++;
			temp = mRegisters[(rm >> 3) & 0x07];
			mRegisters[(rm >> 3) & 0x07] = mRegisters[rm & 0x07];
			mRegisters[rm & 0x07] = temp;
			break;
		case 0x35: //xor eax, imm32
			XOR32_FLAGS(flags_ptr, eax, MEM_GET_UINT32(ip));
			ip += 4;
			break;
		case 0x31: //xor rm, reg
			rm = *ip++;
			if ((rm & 0xC0) == 0xC0) //register mode
			{			
				XOR32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], mRegisters[(rm >> 3) & 0x07]);
			} else {
				int size;
				bool isAddr;
				unsigned int val = decodeRM(rm, &size, &isAddr);
				ip += size;
				XOR32_FLAGS(flags_ptr, reinterpret_cast<unsigned int*>(val), mRegisters[(rm >> 3) & 0x07]);
			}
			break;
		case 0x33: //xor reg, rm
			rm = *ip++;
			if ((rm & 0xC0) == 0xC0) //register mode
			{
				XOR32_FLAGS(flags_ptr, &mRegisters[(rm >> 3) & 0x07], mRegisters[rm & 0x07]);
			} else if ((rm & 0xC7) == 0x05)
			{					
				XOR32_FLAGS(flags_ptr, &mRegisters[(rm >> 3) & 0x07], MEM_GET_UINT32(MEM_GET_UINT32(ip)));
				ip += 4;
			} else {
				int size;
				bool isAddr;
				unsigned int val = decodeRM(rm, &size, &isAddr);
				ip += size;
				XOR32_FLAGS(flags_ptr, &mRegisters[(rm >> 3) & 0x07], MEM_GET_UINT32(val));
			}
			break;
		case 0xFC: //CLD
			mFlags &= ~_DF;
			mDirectionFlag = false;
			break;
		case 0xFD: //STD
			mFlags |= _DF;
			mDirectionFlag = true;
			break;
		case 0x40: //inc eax
			INC32_FLAGS(flags_ptr, eax);			
			break;
		case 0x41: //inc ecx
			INC32_FLAGS(flags_ptr, ecx);
			break;
		case 0x42: //inc edx
			INC32_FLAGS(flags_ptr, edx);
			break;
		case 0x43: //inc ebx
			INC32_FLAGS(flags_ptr, ebx);
			break;
		case 0x44: //inc esp
			INC32_FLAGS(flags_ptr, esp);
			break;
		case 0x45: //inc ebp
			INC32_FLAGS(flags_ptr, ebp);
			break;
		case 0x46: //inc esi
			INC32_FLAGS(flags_ptr, esi);
			break;
		case 0x47: //inc edi
			INC32_FLAGS(flags_ptr, edi);
			break;
		case 0x3B://cmp reg, rm32
			
			rm = *ip++;
			temp = mRegisters[(rm >> 3) & 0x07];
			temp2 = readUINT32(rm);					
			CMP32_FLAGS(flags_ptr, temp, temp2);
			break;
		case 0x39://cmp rm32, reg
			
			rm = *ip++;
			temp = readUINT32(rm);
			temp2 = mRegisters[(rm >> 3) & 0x07];
			CMP32_FLAGS(flags_ptr, temp, temp2);
			break;
		case 0x3D: //cmp eax, imm32
			
			temp = MEM_GET_UINT32(ip);
			ip += 4;
			CMP32_FLAGS(flags_ptr, *eax, temp);			
			break;
		case 0x38: //cmpb rm,reg			
			rm = *ip++;
			{
				unsigned char tmp1 = readUINT8(rm);
				unsigned char tmp2 = *mRegisters8[(rm >> 3) & 0x07];
				CMP8_FLAGS(flags_ptr, tmp1, tmp2);
			}
			break;

		case 0x3A: //cmpb reg,rm
			{				
				rm = *ip++;
				unsigned char tmp1 = (*mRegisters8[(rm >> 3) & 0x07]);
				unsigned char tmp2 = readUINT8(rm);		
				CMP8_FLAGS(flags_ptr, tmp1, tmp2);
				
			}
			break;
		case 0xD3:
			rm = *ip++;
			switch (rm & 0xF8)
			{
			default:
				assert(false);
				break;
			case 0xF8://sar
				SAR32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], *CL);
				break;
			case 0xE0: //shl
				SHL32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], *CL);
				break;
			case 0xE8: //shr
				SHR32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], *CL);				
				break;
			}
			break;
		case 0xD1:
			rm = *ip++;
			switch (rm & 0xF8)
			{
			default:
				assert(false);
				break;
			case 0xF8://sar							
				SAR32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], 1);				
				break;
			case 0xE0: //shl
				SHL32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], 1);
				break;
			case 0xE8: //shr
				SHR32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], 1);
				break;
			}
			break;
		case 0xC1:
			rm = *ip++;
			switch (rm & 0xF8)
			{
			default:
				assert(false);
				break;
			case 0xF8://sar
				SAR32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], *ip);
				ip++;
				break;
			case 0xE0: //shl
				SHL32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], *ip);
				ip++;
				break;
			case 0xE8: //shr
				SHR32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], *ip);
				ip++;
				break;
			}
			break;
		case 0x0F: //conditional jump far
			{			
				switch (*ip++)
				{
				default:
					assert(false);
					break;
					//mov condition
				case 0x40://overflow
					if (mFlags & _OF)
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x41://no overflow
					if (!(mFlags & _OF))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x42: //below
					if ((mFlags & F_BELOW))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x43://not below (>= )
					if (!(mFlags & F_BELOW))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x44://equal
					if (mFlags & F_EQUAL)
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x45://not equal
					if (!(mFlags & F_EQUAL))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x46://F_BELOW_OR_EQUAL

					if (mFlags & (F_BELOW | F_EQUAL))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x47://above
					if (!(mFlags & (F_BELOW | F_EQUAL)))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x48:
					if ((mFlags & F_SIGN))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x49:
					if (!(mFlags & F_SIGN))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x4A:
					if (mFlags & F_PARITY)
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x4B:
					if (!(mFlags & F_PARITY))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x4C://less
					temp = mFlags & F_SIGN;
					temp2 = mFlags & _OF;
					if ((!temp && temp2) || (temp && !temp2))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x4D:// >=
					temp = mFlags & F_SIGN;
					temp2 = mFlags & _OF;
					if ((temp && temp2) || !(temp || temp2))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x4E:// <= 
					temp = mFlags & F_SIGN;
					temp2 = mFlags & _OF;
					if ((!temp && temp2) || (temp && !temp2) || (mFlags & _ZF))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
				case 0x4F:
					temp = mFlags & F_SIGN;
					temp2 = mFlags & _OF;
					if (((temp && temp2) || !(temp || temp2)) && !(mFlags & _ZF))
					{
						rm = *ip++;
						mRegisters[(rm >> 3) & 0x07] = readUINT32(rm);
					}
					break;
					//condition jump far
				case 0x80://overflow
					if (mFlags & _OF)
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x81://no overflow
					if (!(mFlags & _OF))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x82: //below
					if ((mFlags & F_BELOW))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x83://not below (>= )
					if (!(mFlags & F_BELOW))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x84://equal
					if (mFlags & F_EQUAL)
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x85://not equal
					if (!(mFlags & F_EQUAL))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x86://F_BELOW_OR_EQUAL

					if (mFlags & (F_BELOW | F_EQUAL))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x87://above
					if (!(mFlags & (F_BELOW | F_EQUAL)))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x88:
					if ((mFlags & F_SIGN))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x89:
					if (!(mFlags & F_SIGN))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x8A:
					if (mFlags & F_PARITY)
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x8B:
					if (!(mFlags & F_PARITY))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x8C://less
					temp = mFlags & F_SIGN;
					temp2 = mFlags & _OF;
					if ((!temp && temp2) || (temp && !temp2))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x8D:// >=
					temp = mFlags & F_SIGN;
					temp2 = mFlags & _OF;
					if ((temp && temp2) || !(temp || temp2))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x8E:// <= 
					temp = mFlags & F_SIGN;
					temp2 = mFlags & _OF;
					if ((!temp && temp2) || (temp && !temp2) || (mFlags & _ZF))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;
				case 0x8F:
					temp = mFlags & F_SIGN;
					temp2 = mFlags & _OF;
					if (((temp && temp2) || !(temp || temp2)) && !(mFlags & _ZF))
						ip += (int)MEM_GET_UINT32(ip);
					ip += 4;
					break;

					//set flag

				case 0x90://overflow
					if (mFlags & _OF)
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x91://no overflow
					if (!(mFlags & _OF))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x92: //below
					if ((mFlags & F_BELOW))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x93://not below (>= )
					if (!(mFlags & F_BELOW))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x94://equal
					if (mFlags & F_EQUAL)
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);;
					break;
				case 0x95://not equal
					if (!(mFlags & F_EQUAL))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x96://F_BELOW_OR_EQUAL

					if (mFlags & (F_BELOW | F_EQUAL))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x97://above
					if (!(mFlags & (F_BELOW | F_EQUAL)))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x98:
					if ((mFlags & F_SIGN))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x99:
					if (!(mFlags & F_SIGN))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x9A:
					if (mFlags & F_PARITY)
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x9B:
					if (!(mFlags & F_PARITY))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x9C://less
					temp = mFlags & F_SIGN;
					temp2 = mFlags & _OF;
					if ((!temp && temp2) || (temp && !temp2))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x9D:// >=
					temp = mFlags & F_SIGN;
					temp2 = mFlags & _OF;
					if ((temp && temp2) || !(temp || temp2))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x9E:// <= 
					temp = mFlags & F_SIGN;
					temp2 = mFlags & _OF;
					if ((!temp && temp2) || (temp && !temp2) || (mFlags & _ZF))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;
				case 0x9F:
					temp = mFlags & F_SIGN;
					temp2 = mFlags & _OF;
					if (((temp && temp2) || !(temp || temp2)) && !(mFlags & _ZF))
						writeUINT8(*ip++, 1);					
					else
						writeUINT8(*ip++, 0);
					break;

				case 0xBE: //movsx_b REG32, rm8
					rm = *ip++;
					*reinterpret_cast<int*>(&mRegisters[(rm >> 3) & 0x07]) = (int)(char)readUINT8(rm);
					break;
				case 0xBF: //movsx_w  REG32, rm16
					rm = *ip++;
					*reinterpret_cast<int*>(&mRegisters[(rm >> 3) & 0x07]) = (int)(short)readUINT16(rm);				
					break;
				case 0xB6: //movzx_b reg3, rm8
					rm = *ip++;					
					mRegisters[(rm >> 3) & 0x07] = readUINT8(rm);				
					break;			
				case 0xB7: //movzx_w reg3, rm8
					rm = *ip++;
					mRegisters[(rm >> 3) & 0x07] = readUINT16(rm);				
					break;
				case 0xA5: //shld - not used 
					rm = *ip++;
					if ((rm & 0xC0) == 0xC0) //register mode
					{
						_UINT_64 val = mRegisters[rm & 0x07];
						val <<= 32;
						val |= mRegisters[(rm >> 3) & 0x07];
						val <<= *CL;
						mRegisters[rm & 0x07] = (val >> 32) & 0xFFFFFFFF;
						mRegisters[(rm >> 3) & 0x07] = val & 0xFFFFFFFF;
					} else {
						int size;
						bool isAddr;
						unsigned int addr = decodeRM(rm, &size, &isAddr);
						ip += size;
						_UINT_64 val = MEM_GET_UINT32(addr);
						val <<= 32;
						val |= mRegisters[(rm >> 3) & 0x07];
						val <<= *CL;
						MEM_GET_UINT32(addr) = (val >> 32) & 0xFFFFFFFF;
						mRegisters[(rm >> 3) & 0x07] = val & 0xFFFFFFFF;
					}
					break;
				case 0xAD: //shrd - not used
					rm = *ip++;
					if ((rm & 0xC0) == 0xC0) //register mode
					{
						_UINT_64 val = mRegisters[rm & 0x07];
						val <<= 32;
						val |= mRegisters[(rm >> 3) & 0x07];
						val >>= *CL;
						mRegisters[rm & 0x07] = (val >> 32) & 0xFFFFFFFF;
						mRegisters[(rm >> 3) & 0x07] = val & 0xFFFFFFFF;
					} else {
						int size;
						bool isAddr;
						unsigned int addr = decodeRM(rm, &size, &isAddr);
						ip += size;
						_UINT_64 val = MEM_GET_UINT32(addr);
						val <<= 32;
						val |= mRegisters[(rm >> 3) & 0x07];
						val >>= *CL;
						MEM_GET_UINT32(addr) = (val >> 32) & 0xFFFFFFFF;
						mRegisters[(rm >> 3) & 0x07] = val & 0xFFFFFFFF;
					}
					break;
				case 0xA3: //bit test rm, reg
					rm = *ip++;
					if ((rm & 0xC0) == 0xC0) //register mode
					{
						int bitIndex = 1 << (mRegisters[(rm >> 3) & 0x07] & 0x1F);
                        if (mRegisters[rm & 0x07] & bitIndex)
                            mFlags |= _CF;
                        else mFlags &= ~_CF;
						
					} else {
						int size;
						bool isAddr;
						unsigned int addr = decodeRM(rm, &size, &isAddr);
						ip += size;
						int bitIndex = 1 << (mRegisters[(rm >> 3) & 0x07] & 0x1F);
                        if (MEM_GET_UINT32(addr) & bitIndex)
                            mFlags |= _CF;
                        else mFlags &= ~_CF;
                        MEM_GET_UINT32(addr) |= bitIndex;
					}				
					break;
				case 0xAB: //bit test rm, reg test &set
					rm = *ip++;
					if ((rm & 0xC0) == 0xC0) //register mode
					{						
                        int bitIndex = 1 << (mRegisters[(rm >> 3) & 0x07] & 0x1F);
                        if (mRegisters[rm & 0x07] & bitIndex)
                            mFlags |= _CF;
                        else mFlags &= ~_CF;

						mRegisters[rm & 0x07] |= bitIndex;
					} else {
						int size;
						bool isAddr;
						unsigned int addr = decodeRM(rm, &size, &isAddr);
						ip += size;

                        int bitIndex = 1 << (mRegisters[(rm >> 3) & 0x07] & 0x1F);
                        if (MEM_GET_UINT32(addr) & bitIndex)
                            mFlags |= _CF;
                        else mFlags &= ~_CF;
                        MEM_GET_UINT32(addr) |= bitIndex;
						
					}				
					break;
				case 0xAF:	//imul reg, rm			
					{
						rm = *ip++;
						_CLEAR_ALU_FLAGS;
						
						_INT_64 result = (_INT_64)mRegisters[(rm >> 3) & 0x07] * (_INT_64)readUINT32(rm);
						if (((result >> 32) & 0xFFFFFFFF) < 0xFFFFFFFF) {
							mFlags |= _OF | _CF;
						} else mFlags &= ~(_OF | _CF);

						mRegisters[(rm >> 3) & 0x07] = (unsigned int)(result & 0xFFFFFFFF);
					}
					break;
				}
			}
			break;
		case 0x81:	//algorith 32 bits
			rm = *ip++;
			switch ((rm >> 3)& 0x07)
			{
			default:
				assert(false);
				break;
			case 0x00:	//add rm, im32		
				{
					oldIP = ip;
					temp = readUINT32(rm);
					temp2 = MEM_GET_UINT32(ip);
					ADD32_FLAGS(flags_ptr, &temp, temp2);
					ip = oldIP;
					writeUINT32(rm, temp);
					ip += 4;
				}
				break;
			case 0x04: //and rm,im32
				{
					oldIP = ip;
					temp = readUINT32(rm);
					AND32_FLAGS(flags_ptr, &temp, MEM_GET_UINT32(ip));
					ip = oldIP;
					writeUINT32(rm, temp);
					ip += 4;					
				}
				break;
			case 0x07: //cmp rm,im32
				{					
					temp = readUINT32(rm);
					temp2 = MEM_GET_UINT32(ip);
					CMP32_FLAGS(flags_ptr, temp, temp2);					
					ip += 4;
				}
				break;
			case 0x01: //or rm,im32
				{
					if ((rm & 0xC0) == 0xC0) //register mode
					{
						OR32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], MEM_GET_UINT32(ip));
						ip += 4;						
					} else {
						int size;
						bool isAddr;
						unsigned int addr = decodeRM(rm, &size, &isAddr);
						ip += size;
						OR32_FLAGS(flags_ptr, reinterpret_cast<unsigned int*>(addr), MEM_GET_UINT32(ip));
						ip += 4;						
					}
				}
				break;
			case 0x05: //sub rm,im32
				{
					oldIP = ip;
					temp = readUINT32(rm);
					temp2 = MEM_GET_UINT32(ip);
					SUB32_FLAGS(flags_ptr, &temp, temp2);
					ip = oldIP;
					writeUINT32(rm, temp);
				
					ip += 4;				
				}
				break;
			case 0x06: //xor rm,im32
				{
					oldIP = ip;
					temp = readUINT32(rm);
					XOR32_FLAGS(flags_ptr, &temp, MEM_GET_UINT32(ip));
					ip = oldIP;
					writeUINT32(rm, temp);					
					ip += 4;				
				}
				break;
			}
			break;
		case 0x83:  //algorith 8 bits
			rm = *ip++;
			switch ((rm >> 3) & 0x07)
			{
			case 0x00:	//add rm, im8		
				{
					oldIP = ip;
					temp = readUINT32(rm);
					temp2 = (unsigned int)(int)((char)*ip);
					ADD32_FLAGS(flags_ptr, &temp, temp2);

					ip = oldIP;
					writeUINT32(rm, temp);
					ip++;
				}
				break;
			case 0x04:	//and rm, im8		
				{
					oldIP = ip;
					temp = readUINT32(rm);
					temp2 = (unsigned int)(int)((char)(*ip));
					AND32_FLAGS(flags_ptr, &temp, temp2);
					ip = oldIP;
					writeUINT32(rm, temp);					
					ip++;
				}
				break;

			case 0x07: //cmp rm,im8
				{										
					temp = readUINT32(rm);
					temp2 = (int)((char)(*ip++));
					CMP32_FLAGS(flags_ptr, temp, temp2);
				}
				break;
			case 0x01: //or rm,im8
				{
					if ((rm & 0xC0) == 0xC0) //register mode
					{
						OR32_FLAGS(flags_ptr, &mRegisters[rm & 0x07], (int)(char)(*ip));
						ip++;
						
					} else {
						int size;
						bool isAddr;
						unsigned int addr = decodeRM(rm, &size, &isAddr);
						ip += size;
						OR32_FLAGS(flags_ptr, reinterpret_cast<unsigned int*>(addr), (int)((char)(*ip)));
                        ip++;
						
					}
				}
				break;
			case 0x05: //sub rm,im8
				{
					oldIP = ip;
					temp = readUINT32(rm);
					temp2 = (unsigned int)(int)(char)*ip;
					SUB32_FLAGS(flags_ptr, &temp, temp2);
					ip = oldIP;
					writeUINT32(rm, temp);
					ip++;
				}
				break;
			case 0x06: //xor rm,im8
				{
					oldIP = ip;
					temp = readUINT32(rm);
					XOR32_FLAGS(flags_ptr, &temp, (int)(char)*ip);
					ip = oldIP;
					writeUINT32(rm, temp);
					
					ip++;				
				}
				break;
			}		
			break;
		}
	}
}
}
}
/**/