#pragma once

#include "Common.hpp"

#include <unordered_map>

struct Well
{
int id;
int x;
int y;
};

struct WellLayout
{
int referenceWidth;
int referenceHeight;
int radius;

std::vector<Well> wells;

cv::Mat mask;

};

class WellMasker
{
public:

bool initialize();

bool apply(cv::Mat& frame, int wellsCount);

// Retorna o layout (coordenadas + raio) do numero de pocos
// informado, ou nullptr se ainda nao foi carregado.
const WellLayout* getLayout(int wellsCount) const;

private:

bool loadLayout(int wellsCount);

std::unordered_map<int, WellLayout> layouts_;

};
