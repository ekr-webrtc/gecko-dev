/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPChild.h"
#include "GMPVideoDecoderChild.h"
#include "GMPVideoEncoderChild.h"
#include "GMPVideoHost.h"
#include "nsIFile.h"
#include "nsXULAppAPI.h"
#include "gmp-video-decode.h"
#include "gmp-video-encode.h"
#include "GMPPlatform.h"
#include <iostream>

#ifdef XP_WIN
#include <stdlib.h> // for _exit()
#else
#include <unistd.h> // for _exit()
#endif

namespace mozilla {
namespace gmp {

GMPChild::GMPChild()
  : mLib(nullptr)
  , mGetAPIFunc(nullptr)
  , mGMPMessageLoop(MessageLoop::current())
{
  std::cerr << "PID=" << getpid() << std::endl;
  PR_Sleep(30000);
}

GMPChild::~GMPChild()
{
}

void child_exit(void) {
  std::cerr << "SUBPROCESS: EXIT\n";
}

bool
GMPChild::Init(const std::string& aPluginPath,
               base::ProcessHandle aParentProcessHandle,
               MessageLoop* aIOLoop,
               IPC::Channel* aChannel)
{

  return LoadPluginLibrary(aPluginPath) &&
         Open(aChannel, aParentProcessHandle, aIOLoop);
}

bool
GMPChild::LoadPluginLibrary(const std::string& aPluginPath)
{
  nsDependentCString pluginPath(aPluginPath.c_str());

  std::cerr << "SUBPROCESS\n";

  nsCOMPtr<nsIFile> libFile;
  nsresult rv = NS_NewNativeLocalFile(pluginPath, true, getter_AddRefs(libFile));
  if (NS_FAILED(rv)) {
    return false;
  }

  std::cerr << "SUBPROCESS:" << __FUNCTION__ << ":" << __LINE__ << "\n";
  nsAutoString leafName;
  if (NS_FAILED(libFile->GetLeafName(leafName))) {
    return false;
  }
  nsAutoString baseName(Substring(leafName, 4, leafName.Length() - 1));

  std::cerr << "SUBPROCESS:" << __FUNCTION__ << ":" << __LINE__ << "\n";
#if defined(XP_MACOSX)
  nsAutoString binaryName = NS_LITERAL_STRING("lib") + baseName + NS_LITERAL_STRING(".dylib");
#elif defined(OS_POSIX)
  nsAutoString binaryName = NS_LITERAL_STRING("lib") + baseName + NS_LITERAL_STRING(".so");
#elif defined(XP_WIN)
  nsAutoString binaryName =                            baseName + NS_LITERAL_STRING(".dll");
#else
#error not defined
#endif
  libFile->AppendRelativePath(binaryName);

  std::cerr << "SUBPROCESS:" << __FUNCTION__ << ":" << __LINE__ << "\n";
  nsAutoCString nativePath;
  libFile->GetNativePath(nativePath);
  std::cerr << "SUBPROCESS:" << __FUNCTION__ << ":" << __LINE__ << "\n";  
  mLib = PR_LoadLibrary(nativePath.get());
  if (!mLib) {
    std::cerr << "SUBPROCESS:" << __FUNCTION__ << ":" << __LINE__ << "\n";  
    return false;
  }

  GMPInitFunc initFunc = reinterpret_cast<GMPInitFunc>(PR_FindFunctionSymbol(mLib, "GMPInit"));
  if (!initFunc) {
    return false;
  }

  std::cerr << "SUBPROCESS:" << __FUNCTION__ << ":" << __LINE__ << "\n";
  auto platformAPI = new GMPPlatformAPI();
  InitPlatformAPI(*platformAPI);

  if (initFunc(platformAPI) != GMPNoErr) {
    return false;
  }

  std::cerr << "SUBPROCESS:" << __FUNCTION__ << ":" << __LINE__ << "\n";

  mGetAPIFunc = reinterpret_cast<GMPGetAPIFunc>(PR_FindFunctionSymbol(mLib, "GMPGetAPI"));
  if (!mGetAPIFunc) {
    return false;
  }

  std::cerr << "SUBPROCESS:" << __FUNCTION__ << ":" << __LINE__ << "\n";
  return true;
}

MessageLoop*
GMPChild::GMPMessageLoop()
{
  return mGMPMessageLoop;
}

void
GMPChild::ActorDestroy(ActorDestroyReason aWhy)
{
  if (mLib) {
    GMPShutdownFunc shutdownFunc = reinterpret_cast<GMPShutdownFunc>(PR_FindFunctionSymbol(mLib, "GMPShutdown"));
    if (shutdownFunc) {
      shutdownFunc();
    }
  }

  if (AbnormalShutdown == aWhy) {
    NS_WARNING("Abnormal shutdown of GMP process!");
    std::cerr << "SUBPROCESS: abnormal shutdown\n";
    _exit(0);
  }

  XRE_ShutdownChildProcess();
}

void
GMPChild::ProcessingError(Result aWhat)
{
  switch (aWhat) {
    case MsgDropped:
      std::cerr << "SUBPROCESS: processing error\n";
      _exit(0); // Don't trigger a crash report.
    case MsgNotKnown:
      MOZ_CRASH("aborting because of MsgNotKnown");
    case MsgNotAllowed:
      MOZ_CRASH("aborting because of MsgNotAllowed");
    case MsgPayloadError:
      MOZ_CRASH("aborting because of MsgPayloadError");
    case MsgProcessingError:
      MOZ_CRASH("aborting because of MsgProcessingError");
    case MsgRouteError:
      MOZ_CRASH("aborting because of MsgRouteError");
    case MsgValueError:
      MOZ_CRASH("aborting because of MsgValueError");
    default:
      MOZ_CRASH("not reached");
  }
}

PGMPVideoDecoderChild*
GMPChild::AllocPGMPVideoDecoderChild()
{
  return new GMPVideoDecoderChild(this);
}

bool
GMPChild::DeallocPGMPVideoDecoderChild(PGMPVideoDecoderChild* aActor)
{
  delete aActor;
  return true;
}

PGMPVideoEncoderChild*
GMPChild::AllocPGMPVideoEncoderChild()
{
  return new GMPVideoEncoderChild(this);
}

bool
GMPChild::DeallocPGMPVideoEncoderChild(PGMPVideoEncoderChild* aActor)
{
  delete aActor;
  return true;
}

bool
GMPChild::RecvPGMPVideoDecoderConstructor(PGMPVideoDecoderChild* aActor)
{
  auto vdc = static_cast<GMPVideoDecoderChild*>(aActor);

  void* vd = nullptr;
  GMPErr err = mGetAPIFunc("decode-video", &vdc->Host(), &vd);
  if (err != GMPNoErr || !vd) {
    return false;
  }

  vdc->Init(static_cast<GMPVideoDecoder*>(vd));

  std::cerr << "SUBPROCESS:" << __FUNCTION__ << ":" << __LINE__ << "\n";
  return true;
}

bool
GMPChild::RecvPGMPVideoEncoderConstructor(PGMPVideoEncoderChild* aActor)
{
  auto vec = static_cast<GMPVideoEncoderChild*>(aActor);

  std::cerr << "SUBPROCESS:" << __FUNCTION__ << ":" << __LINE__ << "\n";

  void* ve = nullptr;
  GMPErr err = mGetAPIFunc("encode-video", &vec->Host(), &ve);
  if (err != GMPNoErr || !ve) {
    return false;
  }

  vec->Init(static_cast<GMPVideoEncoder*>(ve));

  std::cerr << "SUBPROCESS:" << __FUNCTION__ << ":" << __LINE__ << "\n";

  return true;
}

} // namespace gmp
} // namespace mozilla
