
/*
	r4xh4x.cpp - h4x extesions

	Notes:
		* it'd be nice to move toolbar deetz here, but Rack doesn't include a header
		  entry for toolbar hierarchy -- migrate when it's added


	This file is covered by the LICENSING file in the root of this project.
*/

#include "app.hpp"
#include "plugin.hpp"
#include "window.hpp"
#include "asset.hpp"
#include <map>
#include <vector>
#include "osdialog.h"
#include "unistd.h"
#include "r4xh4x.hpp"

namespace rack {

bool gPatchInsertMode = false;

/*
	generate full catalog.json adding width and other runtime accessible metadata
	when running in main Rack thread -- disable when using in plugin module
*/

void R4xH4x::catalog(bool inRackMainThread) { // serializer gPlugins as JSON
	if (osdialog_message(OSDIALOG_INFO, OSDIALOG_OK_CANCEL,
			"Generate a fresh catalog of your installed plugins modules?\n\n"
			"Cataloging takes a few moments and begins by clearing the current rack, "
			"so press Cancel and save your patch if you have pending changes you "
			"want to keep.\n\n"
			"You only need to regenerate a fresh catalog when you install/uninstall "
			"new plugins.\n\n"
			"Your catalog will be stored in your Rack directory as \"catalog.json\". "
			"rackcli uses this catalog for all sorts of additional features such as "
			"import/export, layout management, automatic module composition, etc. and "
			"is available at:\n\n     https://github.com/dirkleas/rackcli.\n\n")) {
		glfwPollEvents(); // allow dialog to close
		int clear = 0; // periodically clear rendered moduleWidgets

		// map for segfaulting modules from optional faults.json,
		// 	 width resolved with DLwigglz r4xh4x [patch] button => patch.json + p2f
		std::map<std::vector<std::string>, std::vector<int>> faults;
		FILE *ffaults = fopen(assetLocal("faults.json").c_str(), "r");
		if (ffaults) {
			info("Caching faults");
			json_error_t error;
			json_t *faultsJ = json_loadf(ffaults, 0, &error);
			if (faultsJ) {
				size_t index; json_t *value;
				json_array_foreach(faultsJ, index, value) {
					faults[{json_string_value(json_object_get(value, "plugin")),
						json_string_value(json_object_get(value, "version")),
						json_string_value(json_object_get(value, "model"))}] = {
							(int) json_integer_value(json_object_get(value, "width")),
							(int) json_integer_value(json_object_get(value, "height")),
							(int) json_integer_value(json_object_get(value, "paramCount")),
							(int) json_integer_value(json_object_get(value, "inputCount")),
							(int) json_integer_value(json_object_get(value, "outputCount"))
						};
				}
			}
			else {
				warn("JSON parsing error at %s %d:%d %s", error.source, error.line,
					error.column, error.text);
			}
			json_decref(faultsJ);
			fclose(ffaults);
		}

		int xpos, ypos;
		glfwGetWindowPos(gWindow, &xpos, &ypos);
		Vec windowSize = windowGetWindowSize();
		info("Generating catalog for installed plugin models");
		gRackWidget->save(assetLocal("autosave.catalog.vcv")); // save pre-cataloging patch
		json_t *catalogJ = json_object();
		json_t *pluginsJ = json_array();
		json_t *allTagsJ = json_array();
		for (std::string tag : gTagNames) {
			if (!tag.empty()) json_array_append(allTagsJ, json_string(tag.c_str()));
		}
		json_object_set_new(catalogJ, "applicationName", json_string(gApplicationName.c_str()));
		json_object_set_new(catalogJ, "applicationVersion", json_string(gApplicationVersion.c_str()));
		json_object_set_new(catalogJ, "apiHost", json_string(gApiHost.c_str()));
		json_object_set_new(catalogJ, "pixelRatio", json_integer(gPixelRatio));
		json_object_set_new(catalogJ, "xpos", json_integer(xpos));
		json_object_set_new(catalogJ, "ypos", json_integer(ypos));
		json_object_set_new(catalogJ, "width", json_integer(windowSize.x));
		json_object_set_new(catalogJ, "height", json_integer(windowSize.y));
		json_object_set_new(catalogJ, "token", json_string(gToken.c_str()));
		json_object_set_new(catalogJ, "path", json_string(getcwd(NULL, 0)));
		json_object_set_new(catalogJ, "tags", allTagsJ);
		json_object_set_new(catalogJ, "pluginCount", json_integer(gPlugins.size()));
		for (Plugin *plugin : gPlugins) {
			json_t *pluginJ = json_object();
			json_object_set_new(pluginJ, "slug", json_string(plugin->slug.c_str()));
			json_object_set_new(pluginJ, "path", json_string(plugin->path.c_str()));
			json_object_set_new(pluginJ, "version", json_string(plugin->version.c_str()));
			json_object_set_new(pluginJ, "modelCount", json_integer(plugin->models.size()));
			json_t *modelsJ = json_array();
			json_object_set_new(pluginJ, "models", modelsJ);
			for (Model *model : plugin->models) {
				json_t *modelJ = json_object();
				json_object_set_new(modelJ, "slug", json_string(model->slug.c_str()));
				json_object_set_new(modelJ, "name", json_string(model->name.c_str()));
				json_object_set_new(modelJ, "author", json_string(model->author.c_str()));
				json_t *tagsJ = json_array();
				for (ModelTag tag : model->tags) {
					json_array_append(tagsJ, json_string(gTagNames[tag].c_str()));
				}
				json_object_set_new(modelJ, "tags", tagsJ);
				std::map<std::vector<std::string>,std::vector<int>>::iterator fault =
					faults.find({plugin->slug, plugin->version, model->slug});
				if (fault != faults.end()) { // current module in faults
					info("*** gathering %s v%s %s fault details ***", plugin->slug.c_str(),
						plugin->version.c_str(), model->slug.c_str());
					json_object_set_new(modelJ, "width", json_integer(int(fault->second[0])));
					json_object_set_new(modelJ, "height", json_integer(int(fault->second[1])));
					json_object_set_new(modelJ, "paramCount", json_integer(int(fault->second[2])));
					json_object_set_new(modelJ, "inputCount", json_integer(int(fault->second[3])));
					json_object_set_new(modelJ, "outputCount", json_integer(int(fault->second[4])));
				}
				else { // instantiate ModuleWidget to get width (code from ModuleBrowser.cpp)
					info("*** plugin module %d: %s %s ***", clear, plugin->slug.c_str(), model->slug.c_str());
					ModuleWidget *moduleWidget = model->createModuleWidget();
					if (!moduleWidget) return;
					gRackWidget->moduleContainer->addChild(moduleWidget);
					moduleWidget->box.pos = gRackWidget->lastMousePos.minus(moduleWidget->box.size.div(2));
					gRackWidget->requestModuleBoxNearest(moduleWidget, moduleWidget->box);
					json_object_set_new(modelJ, "width", json_integer(int(moduleWidget->box.size.x)));
					json_object_set_new(modelJ, "height", json_integer(int(moduleWidget->box.size.y)));
					json_object_set_new(modelJ, "paramCount", json_integer(moduleWidget->params.size()));
					json_object_set_new(modelJ, "inputCount", json_integer(moduleWidget->inputs.size()));
					json_object_set_new(modelJ, "outputCount", json_integer(moduleWidget->outputs.size()));
				}
				json_array_append(modelsJ, modelJ);
				if (++clear % 25 == 0) { gRackWidget->clear(); info(" *** cleaning house %d ***", clear); }
			}
			json_array_append(pluginsJ, pluginJ);
		}
		gRackWidget->clear(); info(" *** cleaning house %d, final ***", clear);
		json_object_set_new(catalogJ, "plugins", pluginsJ);

		FILE *file = fopen(assetLocal("catalog.json").c_str(), "w");
		if (!file) return;
		json_dumpf(catalogJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		json_decref(catalogJ);
		fclose(file);
		gRackWidget->load(assetLocal("autosave.catalog.vcv")); // restore pre-cataloging patch
		osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, "Your plugin module catalog has been saved as \"catalog.json\".");
    // gRackScene->scrollWidget->offset = Vec(0, 0);
		// windowClose();
	}
}

void R4xH4x::normalizeLayout() {
		info("r4xh4x normalizing layout...");
		/*
		iterate through patch modules and look for positional collisions, deleting
		first instance
		*/
		std::map<std::vector<int>, ModuleWidget*> positions;
		std::map<std::vector<int>, ModuleWidget*>::iterator pIt;
		info("  pre-layout, %d modules in the patch", gRackWidget->moduleContainer->children.size());
		for (Widget *w : gRackWidget->moduleContainer->children) {
			ModuleWidget *moduleWidget = dynamic_cast<ModuleWidget*>(w);
			assert(moduleWidget);
			// info("  %s %dx%d (ROWxHP)", moduleWidget->model->slug.c_str(), (int) moduleWidget->box.pos.y, (int) moduleWidget->box.pos.x);
			std::vector<int> p = {(int) moduleWidget->box.pos.x, (int) moduleWidget->box.pos.y};
			pIt = positions.find(p);
			if (pIt != positions.end()) { // collision, delete original from positions  and rack
				info("  positional collision for %s at %dx%d (ROWxHP)", pIt->second->model->slug.c_str(), p[1], p[0]);
				gRackWidget->deleteModule(pIt->second);
				pIt->second->finalizeEvents();
				delete pIt->second;
				positions.erase(pIt);
			}
			positions[p] = moduleWidget;
		}
		info("  post-layout, %d modules in the patch", gRackWidget->moduleContainer->children.size());
		positions.clear();
}

json_t *R4xH4x::settingsToJson() {
	json_t *rootJ = json_object();
	json_object_set_new(rootJ, "patchInsertMode", json_boolean(gPatchInsertMode));
	return(rootJ);
}

void R4xH4x::settingsFromJson(json_t *rootJ) {
	info("json: %s", json_dumps(rootJ, NULL));
	json_t *patchInsertMode = json_object_get(rootJ, "patchInsertMode");
	if (patchInsertMode) gPatchInsertMode = json_is_true(patchInsertMode);
	info("r4xh4x setting gPatchInsertMode to %s", gPatchInsertMode ? "true" : "false");
}

void R4xH4x::settingsSave(std::string filename) {
	info("Saving settings %s", filename.c_str());
	json_t *rootJ = settingsToJson();
	if (rootJ) {
		FILE *file = fopen(filename.c_str(), "w");
		if (!file) return;
		json_dumpf(rootJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		json_decref(rootJ);
		fclose(file);
	}
}

void R4xH4x::settingsLoad(std::string filename) {
	info("Loading settings %s", filename.c_str());
	FILE *file = fopen(filename.c_str(), "r");
	if (!file) return;
	json_error_t error;
	json_t *rootJ = json_loadf(file, 0, &error);
	if (rootJ) {
		settingsFromJson(rootJ);
		json_decref(rootJ);
	}
	else {
		warn("JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
	}
	fclose(file);
}

} // rack namespace
