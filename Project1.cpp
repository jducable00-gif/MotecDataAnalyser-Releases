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
#include <wchar.h>            // wcstok, _snwprintf (parsing de la ligne de commande /updated)
#include "ReleaseInfo.h"      // markJustUpdated() — anti-boucle self-update sur les lances /updated.
#include <mmsystem.h>          // timeBeginPeriod / timeEndPeriod (1 ms timer resolution)
#include <gdiplus.h>           // GDI+ initialization/shutdown must live here, in _tWinMain

// Self-update : quand l instance courante est relancee depuis le chemin .new.exe avec
// "/updated \"<nom d origine>\\"", on renomme cette exe vers son nom canonique (raccourcis)
// des que possible. Retourne true si un renommage a ete effectue ou n'etait pas necessaire.
static bool SelfUpdateRenameFromNewExe(LPTSTR cmdLine, const String& selfPath)
{
	if (!cmdLine || wcsstr(cmdLine, L"/updated") == NULL) return false;

	const std::wstring selfW(selfPath.c_str());
	static const wchar_t* kSuffix = L".new.exe";   // insensible a la casse (Windows).
	bool isNewExe = selfW.size() > 8 && _wcsnicmp(&selfW[selfW.size()-8], kSuffix, 8) == 0;
	if (!isNewExe) return false;

	// Token suivant "/updated" dans une COPIE de la ligne de commande (wcstok modifie son entree).
	std::wstring origNameW;
	bool afterFlag = false;
	wchar_t buf[1024] = {0}; swprintf_s(buf, 1024, L"%ls", cmdLine);   // copie locale (wcstok modifie son entree).
	for (wchar_t* tok = wcstok(buf, L" \t"); tok != NULL; tok = wcstok(NULL, L" \t")) {
		if (_wcsicmp(tok, L"/updated") == 0) { afterFlag = true; continue; }
		if (!afterFlag) continue;
		std::wstring t(tok);
		if (t.size() >= 2 && ((t.front() == L'"') == (t.back() == L'"'))) t = t.substr(1, t.size()-2);   // dequote si present.
		origNameW.swap(t); break;
	}

	const String dirPath = IncludeTrailingPathDelimiter(ExtractFilePath(selfPath));
	if (origNameW.empty()) {   // repli canonique si le nom d origine est absent (/updated sans argument) :
		// on derive le NOM DE FICHIER de notre propre exe en retirant le suffixe ".new" — correct dans les deux environnements
		// (dev -> Project1.exe, install release -> MotecDataAnalyser.exe). L ancien repli dur "Project1.exe" etait faux pour une
		// installation release. Si la derivation echoue aussi : on ne renomme pas plutot que de deviner un nom canonique inexistant.
		size_t slashPos = selfW.find_last_of(L"\\/");            // NOM seul (sans chemin) — le target est dirPath + ce nom.
		std::wstring fname = (slashPos == std::wstring::npos) ? selfW : selfW.substr(slashPos + 1);
		if (fname.size() > 8 && _wcsnicmp(&fname[fname.size()-8], L".new.exe", 8) == 0)
		{
			std::wstring base = fname.substr(0, fname.size() - 4);   // retire ".exe"
			if (base.size() > 4 && _wcsnicmp(&base[base.size()-4], L".new", 4) == 0)
			{
				origNameW = base.substr(0, base.size() - 4);          // retire ".new"
			}
		}
	}
	if (origNameW.empty()) return true;                                                                 // rien a faire : on reste sur .new.exe.

	const String target = dirPath + origNameW.c_str();
	if (_wcsicmp(target.c_str(), selfPath.c_str()) == 0) return true;                                  // deja au bon endroit.

	bool renamedBack = MoveFile(selfPath.c_str(), target.c_str());                                     // best-effort : ignore l echec (l app continue).
	ReleaseInfoLog(renamedBack ? "rename-back .new.exe -> canonical name: OK"
	                          : ("rename-back FAILED gle=" + std::to_string(GetLastError())).c_str());
	return renamedBack;
}
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
		// Chemin reel du processus AVANT toute init VCL : GetModuleFileNameW lit le nom ACTUEL de notre image,
		// alors qu Application->ExeName est capture par Initialize() et reste stale si l exe a ete renomme au demarrage.
		wchar_t selfPathBuf[MAX_PATH] = {0};
		GetModuleFileNameW(NULL, selfPathBuf, MAX_PATH);

		Application->Initialize();
		Application->MainFormOnTaskBar = true;

		// Self-update: when relaunched with /updated the previous exe was renamed to
		// .old.exe. Delete it now that no process holds a lock on it (best-effort).
		if (cmdLine && wcsstr(cmdLine, L"/updated") != NULL) {
			markJustUpdated();   // anti-boucle : cette instance vient d'etre installee -> plus de popup update ce lancement.

			// RETRY LOOP: l ancienne instance a appele Application->Terminate() mais son image (maintenant .old.exe)
			// reste mappee en memoire jusqu'a la fin du teardown VCL/GDI+ — Windows refuse de supprimer un exe encore
			// charge par n importe quel processus. On retente donc quelques secondes au lieu d une seule tentative.
			const String oldExe = IncludeTrailingPathDelimiter(ExtractFilePath(selfPathBuf)) + "MotecDataAnalyser.old.exe";
			bool oldGone = !GetFileAttributes(oldExe.c_str()) || DeleteFile(oldExe.c_str());
			for (int attempt = 1; !oldGone && attempt <= 20; ++attempt) {
				Sleep(300);
				if (!DeleteFile(oldExe.c_str())) {   // trace : on voit exactement quand le verrou est relache
					ReleaseInfoLog(("delete .old.exe attempt " + std::to_string(attempt) +
					    ": FAILED gle=" + std::to_string(GetLastError())).c_str());
				} else {
					ReleaseInfoLog(("delete .old.exe: OK on attempt " + std::to_string(attempt)).c_str());
					break;
				}
			}

			// La nouvelle instance tourne depuis MotecDataAnalyser.new.exe : on la renomme vers
			// son nom d origine (passe en argument) pour que les raccourcis continuent a pointer.
			SelfUpdateRenameFromNewExe(cmdLine, selfPathBuf);   // best-effort ; ignore l echec.
		}

		Application->CreateForm(__classid(TFormMotecSetup), &FormMotecSetup);
		Application->Run();

		// Trace de sortie : si cette ligne n apparait JAMAIS dans le log apres un Terminate(), c est que la boucle VCL ne se termine pas.
		ReleaseInfoLog("instance exiting: Run() returned — GDI+ shutdown + process exit follows");
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
