/* 
	*************************************************************************

	Utils.h

	Author:    Joseph Frangoudes
	Created:   Oct. 2024
	Project:   Scheduler

	Description:   

	  Helpful utility functions

	(c) CCP 2024

	*************************************************************************
*/
#pragma once
#ifndef UTILS_H
#define UTILS_H

#include <string>

#include "stdafx.h"


#include <CcpMacros.h>
#include <CcpTelemetry.h>

const CcpTelemetryCategory& TelemetryCategory();

#define TELEMETRY_ZONE( zoneName ) \
    TelemetryZone CCP_ANONYMOUS_VARIABLE( telemetryZone_ )( TelemetryCategory(), zoneName, __FILE__, __LINE__ )

bool StdStringFromPyObject( PyObject* obj, std::string& str );

#endif //UTILS_H
