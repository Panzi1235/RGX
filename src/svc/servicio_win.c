/* Servicio de Windows: instalación, desinstalación y bucle del SCM. El mismo ocrx.exe hace todo. */
#include "../../include/ocrx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int svc_servir(const ocrx_config *cfg, int puerto, int hilos);
void svc_detener(void);

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define SVC_NOMBRE  "OCRX"
#define SVC_TITULO  "OCRX - Lectura de comprobantes SUNAT"
#define SVC_DESC    "Servicio HTTP local que lee comprobantes SUNAT (PDF, XML, fotos) y devuelve JSON."

static SERVICE_STATUS g_estado;
static SERVICE_STATUS_HANDLE g_hestado;
static ocrx_config g_cfg; static int g_puerto;

static void reportar(DWORD estado, DWORD codigo, DWORD espera) {
    static DWORD chk = 1;
    g_estado.dwCurrentState = estado; g_estado.dwWin32ExitCode = codigo; g_estado.dwWaitHint = espera;
    g_estado.dwControlsAccepted = (estado == SERVICE_START_PENDING) ? 0 : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_estado.dwCheckPoint = (estado == SERVICE_RUNNING || estado == SERVICE_STOPPED) ? 0 : chk++;
    SetServiceStatus(g_hestado, &g_estado);
}
static void WINAPI control(DWORD ctl) {
    if (ctl == SERVICE_CONTROL_STOP || ctl == SERVICE_CONTROL_SHUTDOWN) { reportar(SERVICE_STOP_PENDING, NO_ERROR, 5000); svc_detener(); }
}
static DWORD WINAPI hilo_servidor(LPVOID p) { (void)p; svc_servir(&g_cfg, g_puerto, g_cfg.hilos); return 0; }

static void WINAPI svc_main(DWORD argc, LPSTR *argv) {
    (void)argc; (void)argv;
    g_hestado = RegisterServiceCtrlHandlerA(SVC_NOMBRE, control);
    if (!g_hestado) return;
    g_estado.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    reportar(SERVICE_START_PENDING, NO_ERROR, 10000);
    /* trabajar desde la carpeta del ejecutable (models\, ocrx.ini) */
    char ruta[MAX_PATH]; GetModuleFileNameA(NULL, ruta, MAX_PATH); char *b = strrchr(ruta, '\\'); if (b) { *b = 0; SetCurrentDirectoryA(ruta); }
    HANDLE h = CreateThread(NULL, 0, hilo_servidor, NULL, 0, NULL);
    reportar(SERVICE_RUNNING, NO_ERROR, 0);
    if (h) { WaitForSingleObject(h, INFINITE); CloseHandle(h); }
    reportar(SERVICE_STOPPED, NO_ERROR, 0);
}

int svc_main_windows(const ocrx_config *cfg, int puerto) {
    g_cfg = *cfg; g_puerto = puerto;
    SERVICE_TABLE_ENTRYA tabla[] = { { (LPSTR)SVC_NOMBRE, svc_main }, { NULL, NULL } };
    if (!StartServiceCtrlDispatcherA(tabla)) { fprintf(stderr, "este modo lo invoca el administrador de servicios; use --servir para primer plano\n"); return 1; }
    return 0;
}

static int correr(const char *cmd) { fprintf(stderr, "  > %s\n", cmd); return system(cmd); }

int svc_instalar(int puerto, const char *ruta_exe) {
    (void)ruta_exe;
    char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, MAX_PATH);
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) { fprintf(stderr, "no se pudo abrir el administrador de servicios (ejecute como administrador)\n"); return 1; }
    char cmdline[MAX_PATH + 64]; snprintf(cmdline, sizeof cmdline, "\"%s\" --servicio --puerto %d", exe, puerto);
    SC_HANDLE svc = OpenServiceA(scm, SVC_NOMBRE, SERVICE_ALL_ACCESS);
    if (svc) {
        fprintf(stderr, "el servicio ya existe; actualizando ruta y puerto\n");
        ChangeServiceConfigA(svc, SERVICE_NO_CHANGE, SERVICE_AUTO_START, SERVICE_NO_CHANGE, cmdline, NULL, NULL, NULL, NULL, NULL, SVC_TITULO);
    } else {
        svc = CreateServiceA(scm, SVC_NOMBRE, SVC_TITULO, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, cmdline, NULL, NULL, NULL, NULL, NULL);
        if (!svc) { fprintf(stderr, "no se pudo crear el servicio (error %lu)\n", GetLastError()); CloseServiceHandle(scm); return 1; }
    }
    SERVICE_DESCRIPTIONA d = { (LPSTR)SVC_DESC }; ChangeServiceConfig2A(svc, SERVICE_CONFIG_DESCRIPTION, &d);
    /* reinicio automático si se cae: 5 s, 10 s, 30 s */
    SC_ACTION acc[3] = { { SC_ACTION_RESTART, 5000 }, { SC_ACTION_RESTART, 10000 }, { SC_ACTION_RESTART, 30000 } };
    SERVICE_FAILURE_ACTIONSA fa; memset(&fa, 0, sizeof fa); fa.dwResetPeriod = 86400; fa.cActions = 3; fa.lpsaActions = acc;
    ChangeServiceConfig2A(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);
    /* firewall */
    char fw[512]; snprintf(fw, sizeof fw, "netsh advfirewall firewall delete rule name=\"OCRX %d\" >nul 2>&1 & netsh advfirewall firewall add rule name=\"OCRX %d\" dir=in action=allow protocol=TCP localport=%d >nul", puerto, puerto, puerto);
    correr(fw);
    if (!StartServiceA(svc, 0, NULL)) { DWORD e = GetLastError(); if (e != ERROR_SERVICE_ALREADY_RUNNING) fprintf(stderr, "el servicio se registro pero no arranco (error %lu); revise el visor de eventos\n", e); }
    CloseServiceHandle(svc); CloseServiceHandle(scm);
    fprintf(stderr, "OCRX instalado como servicio (arranque automatico) en el puerto %d.\nPruebe: curl.exe http://localhost:%d/v1/salud\n", puerto, puerto);
    return 0;
}

int svc_desinstalar(void) {
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) { fprintf(stderr, "ejecute como administrador\n"); return 1; }
    SC_HANDLE svc = OpenServiceA(scm, SVC_NOMBRE, SERVICE_ALL_ACCESS);
    if (!svc) { fprintf(stderr, "el servicio no existe\n"); CloseServiceHandle(scm); return 0; }
    SERVICE_STATUS st; ControlService(svc, SERVICE_CONTROL_STOP, &st);
    for (int i = 0; i < 50; i++) { if (!QueryServiceStatus(svc, &st) || st.dwCurrentState == SERVICE_STOPPED) break; Sleep(200); }
    if (!DeleteService(svc)) fprintf(stderr, "no se pudo eliminar (error %lu)\n", GetLastError()); else fprintf(stderr, "servicio eliminado\n");
    CloseServiceHandle(svc); CloseServiceHandle(scm);
    correr("netsh advfirewall firewall delete rule name=\"OCRX 8085\" >nul 2>&1");
    return 0;
}

int svc_estado(void) {
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return 1;
    SC_HANDLE svc = OpenServiceA(scm, SVC_NOMBRE, SERVICE_QUERY_STATUS);
    if (!svc) { printf("OCRX: no instalado\n"); CloseServiceHandle(scm); return 0; }
    SERVICE_STATUS st; QueryServiceStatus(svc, &st);
    const char *t = st.dwCurrentState == SERVICE_RUNNING ? "corriendo" : st.dwCurrentState == SERVICE_STOPPED ? "detenido" : st.dwCurrentState == SERVICE_START_PENDING ? "iniciando" : "otro";
    printf("OCRX: %s\n", t);
    CloseServiceHandle(svc); CloseServiceHandle(scm);
    return 0;
}
#else
int svc_main_windows(const ocrx_config *cfg, int puerto) { (void)cfg; (void)puerto; return 1; }
int svc_instalar(int puerto, const char *ruta_exe) { (void)puerto; (void)ruta_exe; fprintf(stderr, "solo Windows\n"); return 1; }
int svc_desinstalar(void) { fprintf(stderr, "solo Windows\n"); return 1; }
int svc_estado(void) { fprintf(stderr, "solo Windows\n"); return 1; }
#endif
