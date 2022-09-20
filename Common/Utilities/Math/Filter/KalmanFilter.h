#pragma once


/*
	auto kalmanFilter = KalmanFilter(0.01, 3);

	float filterVal = kalmanFilter.filter(getSensorVal());
	Console::LogLn(filterVal);
*/



class KalmanFilter {
private:
	float R; // Обработка шума
	float Q; // Измерение шума
	float A; // Вектор состояния
	float B; // Вектор управления
	float C; // Вектор измерения
	float cov;
	float x; // Расчетный сигнал без шума

	bool primaryInitX = false; // Произошла ли первичная инициализация x



public:

	KalmanFilter(float R, float Q, float A, float B, float C): R(R), Q(Q), A(A), C(C), B(B) {
		cov = (1 / C) * Q * (1 / C);
	}



	// Фильтруем новое значение
	// z - Последнее значение которое нужно отфильтровать
	// u - Вектор управления
	float Filter(float z, float u) {

		// Первичная инициализация x
		if(!primaryInitX) {
			primaryInitX = true;
			x = (1 / C) * z;
			return x;
		}

		// Прогнозирование
		float predX = (A * x) + (B * u); // Предсказываем следующее значение
		float predCov = ((A * cov) * A) + R; // Неопределенность фильтра

		// Усиление
		float K = predCov * C * (1 / ((C * predCov * C) + Q));

		// Коррекция
		x = predX + K * (z - (C * predX));
		cov = predCov - (K * C * predCov);

		return x;
	}


	// Вернуть последнее отфильтрованное измерение
	float LastMeasurement() { return x; }

	// Задать шум измерения (Q)
	void SetMeasurementNoise(float noise) { Q = noise; }

	// Задать шум обработки (R)
	void SetProcessNoise(float noise) { R = noise; }
};
