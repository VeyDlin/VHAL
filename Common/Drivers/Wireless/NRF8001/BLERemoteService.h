#pragma once

#include "BLERemoteAttribute.h"

class BLERemoteService: public BLERemoteAttribute {
public:
	BLERemoteService(const char *uuid) : BLERemoteAttribute(uuid, BLETypeService) {
	}

};
