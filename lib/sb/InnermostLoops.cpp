#include "Superblocks.h"

static void
visitInnermostHelper(std::vector<llvm::Loop*> &innermost,
                     llvm::Loop *loop) {
  if (loop->empty()) {
    innermost.push_back(loop);
  } else {
    for (auto &subloop : *loop) {
      visitInnermostHelper(innermost, subloop);
    }
  }
}


namespace sb {

std::vector<llvm::Loop*>
getInnermostLoops(llvm::LoopInfo &LI) {
  std::vector<llvm::Loop*> innermost;
  for (auto loop : LI) {
    visitInnermostHelper(innermost, loop);
  }
  return innermost;
}

}
