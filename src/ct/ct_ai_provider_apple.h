/*
 * ct_ai_provider_apple.h
 *
 * CtAiProvider on Apple Foundation Models (Apple Intelligence, macOS 26+)
 * through the Swift bridge libct_applefm.dylib loaded at runtime.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#pragma once

#include "ct_ai_provider.h"

#include <mutex>

class CtAiProviderApple : public CtAiProvider
{
public:
    CtAiProviderApple();
    ~CtAiProviderApple() override;

    std::string name() const override { return "Apple Intelligence"; }
    CtAiCapabilities capabilities() const override;
    bool is_loaded() const override { return _loaded; }
    CtAiModelInfo model_info() const override;
    bool load(std::string& error) override;
    void unload() override;
    bool generate(const CtAiRequest& request,
                  const CtAiTokenCallback& on_piece,
                  const CtAiCancelToken& cancel,
                  std::string& error) override;

    /// availability without creating a provider: code of ct_applefm.h (0 = available) and the reason
    static int availability(std::string& reason);
    /// where the bridge library is looked for (bundle Frameworks dir, next to the executable, CT_APPLEFM_LIB)
    static std::vector<std::string> library_candidates();

private:
    struct Bridge;
    static Bridge* _bridge(std::string& error);

    bool _loaded{false};
    int _contextSize{4096};
    mutable std::mutex _mutex; // one generation at a time
};
