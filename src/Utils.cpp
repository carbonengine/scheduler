#include "Utils.h"

const CcpTelemetryCategory& TelemetryCategory()
{
	// Intentionally return a category that may be "empty" if initial registration failed from the
	// module init function. Logging a warning on registration failure here is unnecessarily noisy.
	static const CcpTelemetryCategory& category = CcpTelemetryCategoryRegister( "scheduler" ).first;
	return category;
}

bool StdStringFromPyObject( PyObject* obj, std::string& str )
{

	if( !PyUnicode_Check( obj ) )
	{
		PyErr_SetString( PyExc_TypeError, "value must be a string" );
		return false;
	}

	PyObject* encodedBytesObject = PyUnicode_AsEncodedString( obj, "utf-8", "strict" );

	if( encodedBytesObject == nullptr )
	{
		return false;
	}

	Py_ssize_t strlen;
	char* buffer;
	int res = PyBytes_AsStringAndSize( encodedBytesObject, &buffer, &strlen );

	if( res < 0 )
	{
		Py_DecRef( encodedBytesObject );
		return false;
	}

	str = std::string( buffer, strlen );

	Py_DecRef( encodedBytesObject );

	return true;
}
