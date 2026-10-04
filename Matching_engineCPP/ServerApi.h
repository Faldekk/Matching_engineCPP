#pragma once
#include "crow.h"
#include "LivePaper.h"

void registerApi(crow::SimpleApp& app, LivePaperSession& session);
