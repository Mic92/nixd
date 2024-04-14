/// \file
/// \brief Implementation of [Code Completion].
/// [Code Completion]:
/// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_completion

#include "AST.h"
#include "Convert.h"

#include "nixd/Controller/Controller.h"

#include <boost/asio/post.hpp>

#include <semaphore>
#include <utility>

using namespace nixd;
using namespace lspserver;
using namespace nixf;

namespace {

/// Set max completion size to this value, we don't want to send large lists
/// because of slow IO.
/// Items exceed this size should be marked "incomplete" and recomputed.
constexpr int MaxCompletionSize = 30;

struct ExceedSizeError : std::exception {
  [[nodiscard]] const char *what() const noexcept override {
    return "Size exceeded";
  }
};

void addItem(std::vector<CompletionItem> &Items, CompletionItem Item) {
  if (Items.size() >= MaxCompletionSize) {
    throw ExceedSizeError();
  }
  Items.emplace_back(std::move(Item));
}

class VLACompletionProvider {
  const VariableLookupAnalysis &VLA;

  static CompletionItemKind getCompletionItemKind(const Definition &Def) {
    if (Def.isBuiltin()) {
      return CompletionItemKind::Keyword;
    }
    return CompletionItemKind::Variable;
  }

  /// Collect definition on some env, and also it's ancestors.
  void collectDef(std::vector<CompletionItem> &Items, const EnvNode *Env,
                  const std::string &Prefix) {
    if (!Env)
      return;
    collectDef(Items, Env->parent(), Prefix);
    for (const auto &[Name, Def] : Env->defs()) {
      if (Name.starts_with(
              "__")) // These names are nix internal implementation, skip.
        continue;
      assert(Def);
      if (Name.starts_with(Prefix)) {
        addItem(Items, CompletionItem{
                           .label = Name,
                           .kind = getCompletionItemKind(*Def),
                       });
      }
    }
  }

public:
  VLACompletionProvider(const VariableLookupAnalysis &VLA) : VLA(VLA) {}

  /// Perform code completion right after this node.
  void complete(const nixf::Node &Desc, std::vector<CompletionItem> &Items,
                const ParentMapAnalysis &PM) {
    std::string Prefix; // empty string, accept all prefixes
    if (Desc.kind() == Node::NK_Identifer)
      Prefix = static_cast<const Identifier &>(Desc).name();
    collectDef(Items, upEnv(Desc, VLA, PM), Prefix);
  }
};

/// \brief Provide completions by IPC. Asking nixpkgs provider.
/// We simply select nixpkgs in separate process, thus this value does not need
/// to be cached. (It is already cached in separate process.)
///
/// Currently, this procedure is explicitly blocked (synchronized). Because
/// query nixpkgs value is relatively fast. In the future there might be nixd
/// index, for performance.
class NixpkgsCompletionProvider {

  AttrSetClient &NixpkgsClient;

public:
  NixpkgsCompletionProvider(AttrSetClient &NixpkgsClient)
      : NixpkgsClient(NixpkgsClient) {}

  void resolvePackage(std::vector<std::string> Scope, std::string Name,
                      CompletionItem &Item) {
    std::binary_semaphore Ready(0);
    PackageDescription Desc;
    auto OnReply = [&Ready, &Desc](llvm::Expected<AttrPathInfoResponse> Resp) {
      if (Resp)
        Desc = *Resp;
      Ready.release();
    };
    Scope.emplace_back(std::move(Name));
    NixpkgsClient.attrpathInfo(Scope, std::move(OnReply));
    Ready.acquire();
    // Format "detail" and document.
    Item.documentation = MarkupContent{
        .kind = MarkupKind::Markdown,
        .value = Desc.Description.value_or("") + "\n\n" +
                 Desc.LongDescription.value_or(""),
    };
    Item.detail = Desc.Version.value_or("?");
  }

  /// \brief Ask nixpkgs provider, give us a list of names. (thunks)
  void completePackages(std::vector<std::string> Scope, std::string Prefix,
                        std::vector<CompletionItem> &Items) {
    std::binary_semaphore Ready(0);
    std::vector<std::string> Names;
    auto OnReply = [&Ready,
                    &Names](llvm::Expected<AttrPathCompleteResponse> Resp) {
      if (!Resp) {
        lspserver::elog("nixpkgs evaluator reported: {0}", Resp.takeError());
        Ready.release();
        return;
      }
      Names = *Resp; // Copy response to waiting thread.
      Ready.release();
    };
    // Send request.
    AttrPathCompleteParams Params{std::move(Scope), std::move(Prefix)};
    NixpkgsClient.attrpathComplete(Params, std::move(OnReply));
    Ready.acquire();
    // Now we have "Names", use these to fill "Items".
    for (const auto &Name : Names) {
      if (Name.starts_with(Prefix)) {
        addItem(Items, CompletionItem{
                           .label = Name,
                           .kind = CompletionItemKind::Field,
                           .data = llvm::formatv("{0}", toJSON(Params)),
                       });
      }
    }
  }
};

} // namespace

void Controller::onCompletion(const CompletionParams &Params,
                              Callback<CompletionList> Reply) {
  auto Action = [Reply = std::move(Reply), URI = Params.textDocument.uri,
                 Pos = toNixfPosition(Params.position), this]() mutable {
    std::string File(URI.file());
    if (std::shared_ptr<NixTU> TU = getTU(File, Reply)) [[likely]] {
      if (std::shared_ptr<nixf::Node> AST = getAST(*TU, Reply)) [[likely]] {
        const nixf::Node *Desc = AST->descend({Pos, Pos});
        if (!Desc) {
          Reply(error("cannot find corresponding node on given position"));
          return;
        }
        CompletionList List;
        try {
          const ParentMapAnalysis &PM = *TU->parentMap();
          const VariableLookupAnalysis &VLA = *TU->variableLookup();
          VLACompletionProvider VLAP(VLA);
          VLAP.complete(*Desc, List.items, PM);
          if (havePackageScope(*Desc, VLA, PM)) {
            // Append it with nixpkgs completion
            // FIXME: handle null nixpkgsClient()
            NixpkgsCompletionProvider NCP(*nixpkgsClient());
            auto [Scope, Prefix] = getScopeAndPrefix(*Desc, PM);
            NCP.completePackages(Scope, Prefix, List.items);
          }
          // Next, add nixpkgs provided names.
        } catch (ExceedSizeError &Err) {
          List.isIncomplete = true;
        }
        Reply(std::move(List));
      }
    }
  };
  boost::asio::post(Pool, std::move(Action));
}

void Controller::onCompletionItemResolve(const CompletionItem &Params,
                                         Callback<CompletionItem> Reply) {

  auto Action = [Params, Reply = std::move(Reply), this]() mutable {
    if (Params.data.empty()) {
      Reply(Params);
      return;
    }
    AttrPathCompleteParams Req;
    auto EV = llvm::json::parse(Params.data);
    if (!EV) {
      // If the json value cannot be parsed, this is very unlikely to happen.
      Reply(EV.takeError());
      return;
    }

    llvm::json::Path::Root Root;
    fromJSON(*EV, Req, Root);

    // FIXME: handle null nixpkgsClient()
    NixpkgsCompletionProvider NCP(*nixpkgsClient());
    CompletionItem Resp = Params;
    NCP.resolvePackage(Req.Scope, Params.label, Resp);

    Reply(std::move(Resp));
  };
  boost::asio::post(Pool, std::move(Action));
}