// CertImporter.cpp :
// This application imports DER encoded X509 certificates into the current
// user's personal trust store.
//
// certimporter <path_to_certificate> <system store name (e.g. ROOT)>
//
// If and only if the import was successful, this program exits with status 0.

#include "stdafx.h"
#include <windows.h>
#include <Wincrypt.h>
#include <iostream>
#include <fstream>
#include <tchar.h>
#pragma comment(lib, "crypt32.lib")

using namespace std;

const int STR_BUF_SZ = 4096;
const int STATUS_BAD_PARAMS = 99;

/**
 * Checks whether a certificate with the expectedName exists in the specified
 * certificate store.
 *
 * See http://msdn.microsoft.com/en-us/library/windows/desktop/aa382363(v=vs.85).aspx
 */
int checkExists(HCERTSTORE store, LPCWSTR commonName) {
	PCCERT_CONTEXT cert = CertFindCertificateInStore(
		store,
		X509_ASN_ENCODING,
		0,
		CERT_FIND_SUBJECT_STR,
		commonName,
		NULL);
	if (cert) {
		return 0;
	}
	wcerr << "No certificate was found with common name " << commonName;
	return 2;
}

void reportWindowsError(const char* action) {
  LPTSTR pErrMsg = NULL;
  DWORD errCode = GetLastError();
  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|
      FORMAT_MESSAGE_IGNORE_INSERTS |
      FORMAT_MESSAGE_FROM_HMODULE|
      FORMAT_MESSAGE_FROM_SYSTEM|
      FORMAT_MESSAGE_ARGUMENT_ARRAY,
      GetModuleHandle(_T("crypt32.dll")),
      errCode,
      LANG_NEUTRAL,
      pErrMsg,
      0,
      NULL);
  fprintf(stderr, "Error %s: %lu %s\n", action, errCode, pErrMsg);
}

/**
 * Imports the certificate from the specified file into the given certificate
 * store.  The file must contain DER encoded bytes for an X509 certificate.
 *
 * See http://www.idrix.fr/Root/Samples/capi_pem.cpp
 * See http://msdn.microsoft.com/en-us/library/windows/desktop/aa382037(v=vs.85).aspx
 * See http://blogs.msdn.com/b/alejacma/archive/2008/01/31/how-to-import-a-certificate-without-user-interaction-c-c.aspx
 */
int addCert(HCERTSTORE store, LPCWSTR certFileName) {
	// Open the certificate file
	ifstream certFile;
	certFile.open(certFileName, ios::in | ios::binary | ios::ate);
	if (!certFile.is_open()) {
		wcerr << "Unable to open cert file: " << certFileName << endl;
		return 2;
	}

	// Read the certificate file into memory
	// Note - tellg gives us the size because we opened the file with ios::ate, which puts
	// the cursor at the end of the file.
	streampos size = certFile.tellg();
	char *memblock = new char[size];
	// Now jump back to the beginning of the file and read it into memory
	certFile.seekg(0, ios::beg);
	certFile.read(memblock, size);
	certFile.close();

	// Parse the certificate
	PCCERT_CONTEXT cert = CertCreateCertificateContext(
		X509_ASN_ENCODING,
		(BYTE *)memblock,
		size);
	if (cert == NULL) {
		reportWindowsError("CertCreateCertificateContext");
		return 3;
	}

	if (CertAddCertificateContextToStore(
		store,
		cert,
		CERT_STORE_ADD_REPLACE_EXISTING,
		NULL
		) == FALSE) {
		reportWindowsError("CertAddCertificateContextToStore");
		CertFreeCertificateContext(cert);
		return 4;
	}

	CertFreeCertificateContext(cert);
	return 5;
}

int deleteCert(HCERTSTORE store, LPCWSTR commonName) {
	PCCERT_CONTEXT cert = CertFindCertificateInStore(
		store,
		X509_ASN_ENCODING,
		0,
		CERT_FIND_SUBJECT_STR,
		commonName,
		NULL);

	if (cert) {
		if (CertDeleteCertificateFromStore(cert) == FALSE) {
			reportWindowsError("CertDeleteCertificateFromStore");
			return 5;
		}
		CertFreeCertificateContext(cert);
	} else {
		wcerr << "No certificate found";
		return 6;
	}
	return 0;
}

// See http://www.idrix.fr/Root/Samples/capi_pem.cpp for the basis of this
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(pCmdLine, &argc);
	// Parse arguments
	if (argc < 3) {
		cerr << "Not enough arguments" << endl;
		return STATUS_BAD_PARAMS;
	}

	LPCWSTR action = argv[0];
	LPCWSTR storeName = argv[1];
	LPCWSTR actionData = argv[2];

	// Figure out which action to take
	int(*actionFn)(HCERTSTORE, LPCWSTR);
	DWORD additionalOpenFlags = 0;
	if (wcsncmp(action, L"find", 4) == 0) {
		actionFn = checkExists;
		additionalOpenFlags = CERT_STORE_READONLY_FLAG;
	}
	else if (wcsncmp(action, L"add", 3) == 0) {
		actionFn = addCert;
	}
	else if (wcsncmp(action, L"delete", 6) == 0) {
		actionFn = deleteCert;
	} else {
		cerr << "Invalid action: " << action << endl;
		return STATUS_BAD_PARAMS;
	}

	// Open the system store into which to add the certificate
	// See https://groups.google.com/forum/#!topic/microsoft.public.dotnet.security/iIkP0mkf5f4
	// We use the system store to avoid prompting the user (which is what
	// happens when using the user store).
	HCERTSTORE store = CertOpenStore(
		CERT_STORE_PROV_SYSTEM,
		X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
		0,
		CERT_SYSTEM_STORE_LOCAL_MACHINE | additionalOpenFlags,
		storeName);
	if (store == NULL) {
		reportWindowsError("CertOpenStore");
		return 1;
	}

	return actionFn(store, actionData);
}
