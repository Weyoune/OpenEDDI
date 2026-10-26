// SPDX-License-Identifier: GPL-3.0-only
#include "editor/mcp.hpp"
#include <iostream>
using namespace bmh::editor;
int main() {
  try {
    Session session;
    McpServer server(session);
    auto init =
        server.dispatch({{"jsonrpc", "2.0"},
                         {"id", 1},
                         {"method", "initialize"},
                         {"params", {{"protocolVersion", "2025-11-25"}}}});
    if (init["result"]["serverInfo"]["name"] != "OpenEDDI")
      throw std::runtime_error("wrong product");
    auto tools = server.dispatch(
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}});
    bool backups = false, preview = false;
    for (const auto &t : tools["result"]["tools"]) {
      if (t["name"] == "editor_new")
        throw std::runtime_error("unsupported new world advertised");
      if (t["name"] == "editor_backups")
        backups = true;
      if (t["name"] == "editor_original_preview")
        preview = true;
    }
    if (!backups || !preview)
      throw std::runtime_error("missing legacy tools");
    session.execute("demo");
    auto reply = server.dispatch({{"jsonrpc", "2.0"},
                                  {"id", 3},
                                  {"method", "tools/call"},
                                  {"params", {{"name", "editor_info"}}}});
    if (reply["result"]["isError"] != false)
      throw std::runtime_error("info failed");
    reply = server.dispatch({{"jsonrpc", "2.0"},
                             {"id", 4},
                             {"method", "tools/call"},
                             {"params", {{"name", "editor_save"}}}});
    if (reply["result"]["isError"] != true)
      throw std::runtime_error("demo saved as original");
    Options options;
    if (options.mcp_port != 8766)
      throw std::runtime_error("wrong port");
    World catalog = World::blank();
    catalog.rows("assets") = Json::array(
        {{{"key", "asset:audio"}, {"files", {{"native", "sound.abk"}}}},
         {{"key", "asset:image"}, {"files", {{"native", "image.png"}}}}});
    const auto images = catalog.query({{"table", "assets"}});
    if (images["items"].size() != 1 ||
        images["items"][0]["key"] != "asset:image")
      throw std::runtime_error("non-images in graphics catalogue");
    std::cout << "MCP legacy protocol passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
