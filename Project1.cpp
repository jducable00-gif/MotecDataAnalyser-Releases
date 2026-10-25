//---------------------------------------------------------------------------
// MotecDataAnalyser — Point d'entrée de l'application (WinMain).
//
// Application VCL Win32 (C++ Builder 12 / Clang). Ce fichier initialise le
// framework VCL, crée la fenêtre principale (TFormMotecSetup) puis démarre la
// boucle d'événements. Toute la logique métier se trouve dans
// MotecSetupForm.cpp (interface) et analyze_motec_setup.cpp (analyse).
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------

#include <vcl.h>
#pragma hdrstop
#include <tchar.h>
#include <mmsystem.h>          // timeBeginPeriod / timeEndPeriod (1 ms timer resolution)
#include <gdiplus.h>           // GDI+ initialization/shutdown must live here, in _tWinMain
#pragma comment(lib, "winmm")  // winmm.lib for timeBeginPeriod/timeEndPeriod
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;
//---------------------------------------------------------------------------
USEFORM("MotecSetupForm.cpp", FormMotecSetup);
USEFORM("MotecSettingsForm.cpp", FormMotecSettings);
#include "MotecSetupForm.h"
#include "MotecSettingsForm.h"
//---------------------------------------------------------------------------
int WINAPI _tWinMain(HINSTANCE, HINSTANCE, LPTSTR cmdLine, int)
{
	// Unlock 1 ms timer resolution so TTimer can fire at sub-16 ms intervals.
	timeBeginPeriod(1);

	// GDI+ MUST be initialized before any form is created — the paint handlers in
	// MotecSetupForm use Gdiplus::Graphics/SolidBrush/Font directly. Without a valid
	// token, every GDI+ call corrupts internal heap state (detected by Embarcadero's
	// debug CRT as exception 0xEEDFADE). Shutdown must happen AFTER Application->Run()
	// returns and all forms are destroyed.
	ULONG_PTR gdiplusToken = 0;
	GdiplusStartupInput gdiplusInput;
	Status gdiStatus = GdiplusStartup(&gdiplusToken, &gdiplusInput, NULL);

	try
	{
		Application->Initialize();
		Application->MainFormOnTaskBar = true;

		// Self-update: when relaunched with /updated the previous exe was renamed to
		// .old.exe. Delete it now that no process holds a lock on it (best-effort).
		if (cmdLine && wcsstr(cmdLine, L"/updated") != NULL) {
			const String oldExe = IncludeTrailingPathDelimiter(ExtractFilePath(Application->ExeName)) + "MotecDataAnalyser.old.exe";
			DeleteFile(oldExe.c_str());   // ignore failure — file may already be gone
		}

		Application->CreateForm(__classid(TFormMotecSetup), &FormMotecSetup);
		Application->Run();
	}
	catch (Exception &exception)
	{

		Application->ShowException(&exception);
	}
	catch (...)
	{
		try
		{
			throw Exception("");
		}
		catch (Exception &exception)
		{
			Application->ShowException(&exception);
		}
	}

	// CRITICAL ORDERING: The main form is owned by TApplication and its C++ destructor
	// fires during Application's static cleanup — which happens AFTER _tWinMain returns.
	// If GdiplusShutdown runs before that, any residual GDI+ state in VCL controls or
	// heap guard-bytes allocated while GDI+ was live will trigger 0xEEDFADE at exit.
	// Fix: explicitly destroy the form NOW (while we're still inside _tWinMain), then
	// shut down GDI+. DoFormClose already did all resource cleanup; this just runs the
	// C++ destructor + implicit member destructors in a safe context.
	if (FormMotecSetup) {
		delete FormMotecSetup;   // triggers ~TFormMotecSetup() NOW, before GdiplusShutdown
		FormMotecSetup = nullptr;
	}

	if (gdiplusToken != 0) {
		GdiplusShutdown(gdiplusToken);
		gdiplusToken = 0;
	}

	timeEndPeriod(1);
	return 0;
}
//---------------------------------------------------------------------------
