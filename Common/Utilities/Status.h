#pragma once
#include <Utilities/DataTypes.h>

#define StatusAssert(__e) if(__e != Status::ok) return __e


struct Status {
	typedef enum:uint8 {
		ok,											// Успешно
		error,										// Ошибка
		readError,									// Ошибка чтения
		writeError,									// Ошибка записи
		accessError,								// Ошибка доступа
		interfaceError,								// Ошибка интерфейса
		busy,										// Занято
		ready,										// Готов
		run,										// Работает
		stop,										// Остановлен
		timeout,									// Время ожидания истекло
		notExist,									// Не существует
		notReady,									// Не готов
		notRecognized,								// Не опознано
		notAvailable,								// Недоступен
		notSupported,								// Не поддерживается
		noConnection,								// Нет соединения
		noMatches,									// Нет совпадений
		notFound,									// Не найдено
		noDetected,									// Не обнаружен
		noInit,										// Не инициализирован
		reset,										// Сброшен
		powerFailure,								// Отказ питания
		invalidParameter,							// Неверный параметр
		filled,										// Заполнен
		outOfRange,									// Выход за диапозон
		overheat,									// Перегрев
		undercooling,								// Переохлаждение
		badAddress,									// Неверный адрес
		wrongHashValue,								// Ошибка сравнения подписи
		on,											// Включено
		off,										// Выключено								 
		noSuchFileOrDirectory,						// Данный файл или каталог отсутствует
		noSuchProcess,								// Нет такого процесса
		ioError,						 			// Ошибка ввода / вывода
		noSuchDeviceOrAddress,						// Нет такого устройства или адреса
		argListTooLong,								// Аргумент список слишком длинный
		badFileNumber,								// Неверный номер файла
		tryAgain,									// Попробуйте снова
		outOfMemory,								// Недостаточно памяти
		permissionDenied,							// Доступ запрещен
		deviceOrResourceBusy,						// Устройство или ресурс занят
		fileExists,									// Файл существует
		fileNotExists,								// Файл не существует
		noSuchDevice,								// Нет такого устройства
		notDirectory,								// Это не каталог
		isDirectory,								// Это каталог
		invalidArgument,							// Неверный аргумент
		fileTableOverflow,							// Переполнение таблицы файлов
		tooManyOpenFiles,							// Слишком много открытых файлов
		textFileBusy,								// Текстовый файл занят
		fileTooLarge,								// Файл слишком большой
		noSpaceLeftOnDevice,						// Не осталось свободного места на устройстве
		readOnly,									// Только для чтения
		tooManyLinks,								// Слишком много ссылок
		resourceDeadLock,							// deadlock ресурса
		fileNameTooLong,							// Имя файла слишком длинное
		functionNotImplemented,						// Функция не реализована
		directoryNotEmpty,							// Каталог не пустой
		identifierRemoved,							// Идентификатор удален
		protocolDriverNotAttached,					// Драйвер протокола не подключен
		invalidExchange,							// Неверный обмен
		invalidRequestDescriptor,					// Неверный дескриптор запроса
		invalidRequestCode,							// Неверный код запроса
		invalidSlot,								// Неверный слот
		badFileFormat,								// Неверный формат файла
		noDataAvailable,							// Данные недоступны
		timerExpired,								// Таймер истек
		objectIsRemote,								// Объект удален
		communicationErrorOnSend,					// Ошибка связи при отправке
		protocolError,								// Ошибка протокола
		valueTooLargeforDefinedDataType,			// Слишком большое значение для определенного типа данных
		nameNotUniqueon,							// Имя не уникальное
		fileDescriptorInBadState,					// Файловый дескриптор в плохом состоянии
		illegalByteSequence,					 	// Недопустимая последовательность байтов
		streamsPipeError,						 	// Ошибка потоковой передачи
		tooManyUsers,								// Слишком много пользователей
		destinationAddressRequired,					// Требуется адрес назначения
		messageTooLong,						 		// Сообщение слишком длинное
		addressAlreadyinUse,						// Адрес уже используется
		cannotAssignRequestedAddress,				// Невозможно назначить запрошенный адрес
		networkIsDown,						 		// Сеть не работает
		networkIsUnreachable,						// сеть недоступна
		networkDroppedConnection,					// Сеть разорвала соединение
		noBufferSpaceAvailable,						// Нет доступного буферного пространства
		connectionTimedout,						 	// Время соединения истекло
		connectionRefused,						 	// В соединении отказано
		hostIsdown,								 	// Хост не работает
		operationNowinProgress,						// Операция сейчас в процессе
		empty										// Пустой
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
