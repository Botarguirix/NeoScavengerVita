            {   // memoria viva de bitmaps (con pixeles en este momento), por tamano
                std::map<std::pair<int,int>, std::pair<size_t,size_t>> live; size_t tot = 0;
                for (auto* b : flash::bmpStats().all) if (!b->px.empty()) { auto& e = live[{b->width, b->height}]; e.first++; e.second += b->px.capacity() * 4; tot += b->px.capacity() * 4; }
                std::vector<std::pair<size_t, std::pair<int,int>>> v;
                for (auto& kv : live) v.push_back({kv.second.second, kv.first});
                std::sort(v.rbegin(), v.rend());
                std::printf("[memoria] bitmaps VIVOS: %.1f MB\n", tot / 1048576.0);
                for (size_t i = 0; i < v.size() && i < 10; i++)
                    std::printf("[memoria]   vivos %5dx%-5d x%-5zu = %.1f MB\n", v[i].second.first, v[i].second.second, live[v[i].second].first, v[i].first / 1048576.0);
            }
            std::printf("[memoria] recolector: %zu pasadas, ultima %.1f ms, liberados %zu, vivos %zu objetos\n", as3::gc::stats().collections,
                        as3::gc::stats().lastMs, as3::gc::stats().lastFreed, as3::gc::stats().lastLive);
            {   // que tipos de objeto siguen vivos
                std::map<std::string, size_t> byType;
                for (auto* o : as3::gc::heap()) byType[o->__className().str()]++;
                std::vector<std::pair<size_t, std::string>> t;
                for (auto& kv : byType) t.push_back({kv.second, kv.first});
                std::sort(t.rbegin(), t.rend());
                for (size_t i = 0; i < t.size() && i < 12; i++) std::printf("[memoria]   tipo %-28s %zu\n", t[i].second.c_str(), t[i].first);
            }
            std::printf("[diferido] aplanados %zu (ordenes reproducidas %zu), por escritura de un origen pendiente %zu\n",
                        flash::deferStats().flushes, flash::deferStats().flushedCmds, flash::deferStats().srcFlushes);
            {   // pixeles copiados por software, por quien llama (solo en la compilacion con rastreo)
                std::vector<std::pair<size_t, std::string>> t;
                for (auto& kv : flash::bmpStats().cp) t.push_back({kv.second, kv.first});
                std::sort(t.rbegin(), t.rend());
                for (size_t i = 0; i < t.size() && i < 8; i++) std::printf("[copia] %9.1f Mpx  %s\n", t[i].first / 1e6, t[i].second.c_str());
            }
