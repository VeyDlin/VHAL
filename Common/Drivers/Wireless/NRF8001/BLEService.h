#pragma once
#include "BLELocalAttribute.h"

class BLEService: public BLELocalAttribute {
public:
	BLEService(const char *uuid) : BLELocalAttribute(uuid, BLETypeService) {
	};
};
