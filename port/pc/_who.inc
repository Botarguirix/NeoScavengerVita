            {
                std::vector<std::pair<size_t, std::string>> w;
                for (auto& kv : flash::bmpStats().who) w.push_back({kv.second, kv.first});
                std::sort(w.rbegin(), w.rend());
                for (size_t i = 0; i < w.size() && i < 15; i++) std::printf("[memoria] %6.1f MB  %s\n", w[i].first / 1048576.0, w[i].second.c_str());
            }
