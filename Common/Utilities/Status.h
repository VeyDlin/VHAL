#pragma once
#include <Utilities/DataTypes.h>

#define StatusAssert(__e) if(__e != Status::ok) return __e


struct Status {
	typedef enum:uint8 {
		ok,											
		error,										
		readError,									
		writeError,									
		accessError,								
		interfaceError,								
		busy,										
		ready,										
		run,										
		stop,										
		timeout,									
		notExist,									
		notReady,									
		notRecognized,								
		notAvailable,								
		notSupported,								
		noConnection,								
		noMatches,									
		notFound,									
		noDetected,									
		noInit,										
		reset,										
		powerFailure,								
		invalidParameter,							
		filled,										
		outOfRange,									
		overheat,									
		undercooling,								
		badAddress,									
		wrongHashValue,								
		on,											
		off,										
		noSuchFileOrDirectory,						
		noSuchProcess,								
		ioError,						 			
		noSuchDeviceOrAddress,						
		argListTooLong,								
		badFileNumber,								
		tryAgain,									
		outOfMemory,								
		permissionDenied,							
		deviceOrResourceBusy,						
		fileExists,									
		fileNotExists,								
		noSuchDevice,								
		notDirectory,								
		isDirectory,								
		invalidArgument,							
		fileTableOverflow,							
		tooManyOpenFiles,							
		textFileBusy,								
		fileTooLarge,								
		noSpaceLeftOnDevice,						
		readOnly,									
		tooManyLinks,								
		resourceDeadLock,							
		fileNameTooLong,							
		functionNotImplemented,						
		directoryNotEmpty,							
		identifierRemoved,							
		protocolDriverNotAttached,					
		invalidExchange,							
		invalidRequestDescriptor,					
		invalidRequestCode,							
		invalidSlot,								
		badFileFormat,								
		noDataAvailable,							
		timerExpired,								
		objectIsRemote,								
		communicationErrorOnSend,					
		protocolError,								
		valueTooLargeforDefinedDataType,			 
		nameNotUniqueon,							
		fileDescriptorInBadState,					
		illegalByteSequence,					 	
		streamsPipeError,						 	
		tooManyUsers,								
		destinationAddressRequired,					
		messageTooLong,						 		
		addressAlreadyinUse,						
		cannotAssignRequestedAddress,				
		networkIsDown,						 		
		networkIsUnreachable,						
		networkDroppedConnection,					
		noBufferSpaceAvailable,						
		connectionTimedout,						 	
		connectionRefused,						 	
		hostIsdown,								 	
		operationNowinProgress,						
		empty,
		listen										
	} statusType;

	// Структура, хранящая строку и файл ошибки
	struct errorDebugInfo {
		const char *file;
		int line;
	};


	template<class returnType>
	struct info {
		statusType type;
		returnType data;
		inline bool IsError() { return type != ok; }
		inline bool IsOk() { return type == ok; }
	};

	template<class returnType>
	struct infoDebug {
		statusType type;
		returnType data;
		errorDebugInfo error;
	};


};
