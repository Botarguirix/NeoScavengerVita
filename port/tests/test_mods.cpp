// Prueba del gestor de mods con zips reales y los XML reales del juego.
// Uso: test_mods <carpeta_zips> <carpeta_mods_salida> <SampleModFolder>
#include <cstdio>

#include "../src/modmgr.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { std::printf("FALLO linea %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main(int argc, char** argv) {
    if (argc < 4) { std::printf("uso: test_mods <zips> <salida> <SampleModFolder>\n"); return 2; }
    std::string zips = argv[1], outDir = argv[2], sample = argv[3];
    modmgr::mkdirs(outDir);
    modmgr::ModManager mm(outDir);

    // A) zip habitual con carpeta raiz
    auto a = mm.installZip(zips + "/FancyBag-1.0.zip");
    CHECK(a.ok && a.modName == "FancyBag" && a.files == 3);
    CHECK(modmgr::fileExists(outDir + "/FancyBag/data/itemtypes.xml"));
    // B) plano: nombre de mod = nombre del zip
    auto b = mm.installZip(zips + "/FlatMod.zip");
    CHECK(b.ok && b.modName == "FlatMod" && modmgr::fileExists(outDir + "/FlatMod/data/treasuretable.xml"));
    // C) anidado: la raiz es la carpeta mas superficial que contiene data/
    auto c = mm.installZip(zips + "/Nested-2.zip");
    CHECK(c.ok && c.modName == "Nested" && modmgr::fileExists(outDir + "/Nested/data/itemtypes.xml"));
    CHECK(modmgr::fileExists(outDir + "/Nested/getmods.php"));
    // D) malicioso: se rechaza y no escribe fuera
    auto d = mm.installZip(zips + "/Evil.zip");
    CHECK(!d.ok && d.message.find("peligrosa") != std::string::npos);
    CHECK(!modmgr::fileExists(outDir + "/../escape.txt") && !modmgr::fileExists(outDir + "/escape.txt"));
    // E) no es un mod
    auto e = mm.installZip(zips + "/NotAMod.zip");
    CHECK(!e.ok && e.message.find("no parece un mod") != std::string::npos);
    // F) corrupto
    auto f = mm.installZip(zips + "/Broken.zip");
    CHECK(!f.ok);
    // nombre forzado
    auto g = mm.installZip(zips + "/FancyBag-1.0.zip", "Mi Bolsa");
    CHECK(g.ok && g.modName == "Mi Bolsa");

    // validacion con los XML reales del juego (itemtypes.xml pesa 1,1 MB)
    std::string xml;
    CHECK(modmgr::readFile(sample + "/data/itemtypes.xml", xml));
    std::string err;
    CHECK(modmgr::xmlWellFormed(xml, &err));
    CHECK(!modmgr::xmlWellFormed(xml.substr(0, xml.size() / 2), &err));
    CHECK(!modmgr::xmlWellFormed("<a><b></a>", &err) && err.find("</a>") != std::string::npos);
    CHECK(modmgr::xmlWellFormed("<?xml version=\"1.0\"?><a x=\"1\"><!-- c --><b/><![CDATA[<x>]]></a>"));

    // mod con XML roto: se instala pero queda desactivado con el motivo
    auto h = mm.installZip(zips + "/BadXml.zip");
    CHECK(h.ok);
    modmgr::ModInfo bad; bad.name = "BadXml"; bad.path = outDir + "/BadXml/";
    CHECK(!modmgr::ModManager::validate(bad, {"data/itemtypes.xml"}) && !bad.enabled && !bad.error.empty());
    modmgr::ModInfo good; good.name = "FancyBag"; good.path = outDir + "/FancyBag/";
    CHECK(modmgr::ModManager::validate(good, {"data/itemtypes.xml"}) && good.enabled);

    // getmods.php original
    std::string gm;
    CHECK(modmgr::readFile(sample + "/getmods.php", gm));
    auto refs = modmgr::parseGetMods(gm);
    CHECK(refs.size() == 2 && refs[0].name == "SampleMod" && refs[0].url == "SampleModFolder" && refs[1].name == "0" && refs[1].url == "SampleModFolder/0");

    // orden y activacion
    mm.loadOrder();
    auto mods = mm.scan({"FlatMod", "FancyBag", "Nested", "0"});
    CHECK(mods.size() == 4);
    mods[1].enabled = false;
    CHECK(mm.saveOrder(mods));
    modmgr::ModManager mm2(outDir);
    mm2.loadOrder();
    auto mods2 = mm2.scan({"FlatMod", "FancyBag", "Nested", "0"});
    bool fancyOff = false;
    for (auto& m : mods2) if (m.name == "FancyBag") fancyOff = !m.enabled;
    CHECK(fancyOff);
    CHECK(modmgr::ModManager::toGetMods(mods2).size() == 3);

    std::printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
