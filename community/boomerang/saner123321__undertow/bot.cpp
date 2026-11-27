#include "dart.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <queue>

using namespace dart;

namespace {

struct Vec {
    double x = 0, y = 0;
    Vec operator+(Vec b) const { return {x + b.x, y + b.y}; }
    Vec operator-(Vec b) const { return {x - b.x, y - b.y}; }
    Vec operator*(double k) const { return {x * k, y * k}; }
    double dot(Vec b) const { return x * b.x + y * b.y; }
    double sq() const { return dot(*this); }
    double len() const { return std::sqrt(sq()); }
    Vec unit(double n = 1) const { double length = len(); return length > 0.01 ? *this * (n / length) : Vec{}; }
    Vec left() const { return {-y, x}; }
};

i64 roundInt(double x) { return i64(x >= 0 ? x + 0.5 : x - 0.5); }
Vec point(const Player& p) { return {double(p.x), double(p.y)}; }
Vec speed(const Player& p) { return {double(p.vx), double(p.vy)}; }
Vec point(const Dart& d) { return {double(d.x), double(d.y)}; }
Vec heading(const Player& p) { return {double(p.ax), double(p.ay)}; }
double clip(double x) { return std::clamp(x, 0., 1.); }

struct Body {
    i64 x = 0, y = 0, vx = 0, vy = 0;
    i64 st = S_FREE, timer = 0, ax = 0, ay = 0, cd = 0;

    Body() = default;
    explicit Body(const Player& p)
        : x(p.x), y(p.y), vx(p.vx), vy(p.vy), st(p.st), timer(p.timer),
          ax(p.ax), ay(p.ay), cd(p.cd) {}
};

Vec point(const Body& p) { return {double(p.x), double(p.y)}; }

double segmentDistance2(Vec from, Vec to) {
    Vec d = to - from;
    double n = d.sq();
    return n < 0.01 ? from.sq() : (from + d * clip(-from.dot(d) / n)).sq();
}

Action order(Vec move, int kind = A_NONE, Vec aim = {}) {
    Action a;
    a.mx = roundInt(move.x); a.my = roundInt(move.y);
    a.act = kind;
    a.ax = roundInt(aim.x); a.ay = roundInt(aim.y);
    return a;
}

constexpr int Horizon = 14;
constexpr int Forecast = 24;
constexpr int Models = 8;
constexpr int History = 64;
constexpr int Angles = 24;
constexpr int Grid = CELL / 2;
constexpr double Infinity = 1e12;
constexpr double BlindPenalty = 13.0;
constexpr double DashPenalty = 2.25;
constexpr bool ApproachParry = true;
constexpr bool Crossfire = true;
constexpr bool WeaponClock = true;
constexpr double FfaSeparation = 1.0;
constexpr bool ContextualTracking = true;
constexpr bool ContactInference = true;
constexpr bool CurvedRoutes = true;
constexpr double WeaponDenial = 1.0;
constexpr bool ImpactInference = true;
constexpr bool LockPenalty = true;
constexpr bool CoupledCombat = true;
constexpr bool CatchAwareness = true;

constexpr float hiddenWeights[32][18] = {
    {-5.56284375e-03f, -3.23812693e-01f, -5.18219136e-02f, -1.69147968e-01f, 2.68024236e-01f, 6.68239415e-01f, 5.70917800e-02f, 5.14021255e-02f, -2.32215092e-01f, -2.67825395e-01f, -2.32754201e-01f, 5.29235005e-01f, -1.81411266e-01f, -5.74853756e-02f, -2.36218482e-01f, 5.27037820e-03f, 2.89534420e-01f, 8.34823251e-02f},
    {7.30163902e-02f, -2.36194909e-01f, 7.33023286e-02f, -2.39638969e-01f, -9.91781428e-02f, 2.06468567e-01f, 5.15772849e-02f, 3.80447030e-01f, 4.41802800e-01f, 1.92903116e-01f, -2.30770096e-01f, -7.32140169e-02f, 2.84527361e-01f, 2.81340163e-02f, -5.95421195e-02f, -1.66283831e-01f, 5.46527386e-01f, -3.92096378e-02f},
    {-1.72354281e-02f, -2.99372464e-01f, -5.91224320e-02f, -3.60174268e-01f, 4.97778095e-02f, 2.27108806e-01f, -5.42133898e-02f, -3.20135243e-02f, 4.04812545e-01f, 1.96517229e-01f, 2.15160757e-01f, 8.51163417e-02f, -6.70779049e-01f, -4.65161234e-01f, -7.10068792e-02f, -9.46231112e-02f, 9.03105617e-01f, 8.71941149e-02f},
    {-3.44314784e-01f, 5.43396883e-02f, -1.46011412e-01f, 4.16215658e-01f, 3.65182221e-01f, 1.66429803e-01f, 2.07987782e-02f, -2.54962184e-02f, 1.39206111e-01f, 2.26484075e-01f, -9.15779918e-02f, 1.93317652e-01f, 1.70444567e-02f, -1.09342381e-01f, 3.66378054e-02f, -1.20363235e-01f, -3.34810227e-01f, 7.99263641e-02f},
    {-3.18342149e-01f, -2.90149152e-01f, -2.21080974e-01f, 3.51211093e-02f, 2.82550424e-01f, -1.72818571e-01f, 2.94067740e-01f, -1.98127359e-01f, -9.83502492e-02f, 2.52295017e-01f, 4.11058754e-01f, 1.15488008e-01f, -1.85049810e-02f, 3.06313872e-01f, 1.07957728e-01f, -3.14682305e-01f, 2.65516281e-01f, 7.77275860e-02f},
    {-8.77329558e-02f, 3.46480608e-01f, -4.79610711e-02f, 1.62265033e-01f, 3.39223653e-01f, -5.76988757e-01f, -1.21306060e-02f, -1.74851324e-02f, -4.93755549e-01f, -9.85147178e-01f, -8.96664783e-02f, 4.67769325e-01f, -2.18960881e-01f, -8.05190653e-02f, 2.73567677e-01f, -1.53026925e-02f, 2.00839326e-01f, 5.59007116e-02f},
    {2.97874719e-01f, 4.56300139e-01f, 1.74782246e-01f, -2.55710661e-01f, 1.18967161e-01f, 2.09031522e-01f, -1.18574880e-01f, -1.97521299e-01f, 6.30544424e-02f, 5.16499043e-01f, -3.57860506e-01f, 1.39479265e-01f, 9.32059437e-02f, 6.68133870e-02f, 3.88712808e-02f, 1.14504777e-01f, 2.36216798e-01f, -1.95379764e-01f},
    {-6.98435062e-04f, -1.01276889e-01f, 1.32760722e-02f, -5.04937805e-02f, -2.92609893e-02f, -2.51812547e-01f, 6.49467707e-02f, -2.02639718e-02f, -2.68902123e-01f, 4.26072776e-01f, 4.67986614e-03f, -1.75285544e-02f, 4.45276380e-01f, 8.17888901e-02f, 1.10786624e-01f, -4.59076643e-01f, 5.27337380e-02f, -6.15286119e-02f},
    {-2.47082815e-01f, 1.77179240e-02f, -1.59707159e-01f, -2.77553946e-02f, -7.14757144e-01f, -3.36912163e-02f, 3.44342515e-02f, -1.60810631e-02f, -3.78580898e-01f, 5.83675146e-01f, 2.60772780e-02f, 8.05373788e-02f, 6.25531673e-02f, -1.88287757e-02f, -8.45940113e-02f, -3.68403713e-03f, 4.42134410e-01f, 7.34913647e-02f},
    {-6.85260817e-02f, 2.65854388e-01f, -6.48302808e-02f, 1.31033704e-01f, -5.21671250e-02f, 2.98037112e-01f, -3.19836065e-02f, -2.23483127e-02f, -2.25577369e-01f, 1.33254111e+00f, -5.06938361e-02f, 4.30271208e-01f, 4.62143496e-02f, -2.47550845e-01f, 3.98469716e-02f, -4.63480838e-02f, 1.25436066e-02f, 1.10469878e-01f},
    {7.40252584e-02f, 4.92056310e-02f, 3.06036443e-01f, -5.83659522e-02f, -2.61992842e-01f, 1.14229135e-01f, 3.80861044e-01f, 2.75432020e-02f, 3.33412021e-01f, -5.78806937e-01f, -9.33080725e-03f, 7.32295513e-02f, -2.16030385e-02f, 3.41255851e-02f, 1.78078890e-01f, 9.86312404e-02f, 4.24151808e-01f, -9.96700898e-02f},
    {-4.67856497e-01f, 4.44123834e-01f, -3.80762219e-02f, -1.67074010e-01f, -2.57299524e-02f, -3.04800332e-01f, -9.19217616e-02f, 6.39469996e-02f, 2.80903608e-01f, -2.35630050e-02f, -1.31047189e-01f, 5.23848087e-02f, -2.13334531e-01f, 5.04823364e-02f, 2.58909967e-02f, 2.89117917e-02f, -2.98085719e-01f, 6.55537248e-02f},
    {-6.49695158e-01f, 3.01568776e-01f, 9.19165537e-02f, -1.84877813e-01f, -1.07070096e-01f, 2.33127959e-02f, 2.22841397e-01f, 3.48311812e-02f, 3.91080588e-01f, 3.43802929e-01f, -1.33924797e-01f, -6.33960515e-02f, 7.13120550e-02f, -1.06672756e-01f, 8.83280113e-03f, -1.56881604e-02f, -5.54546490e-02f, 3.82546335e-02f},
    {5.75702414e-02f, 8.23507249e-01f, 8.59237835e-02f, -7.00260222e-01f, -1.63981840e-01f, -6.59082413e-01f, -1.35722216e-02f, -2.82526482e-02f, -3.18018287e-01f, 1.98833439e-02f, 2.10029725e-02f, 1.52582720e-01f, -1.25883594e-01f, -1.54949948e-02f, -5.90469241e-01f, -1.16474524e-01f, -4.18624163e-01f, -3.75660323e-02f},
    {7.16275036e-01f, -2.48474404e-02f, -3.03565919e-01f, -5.28894402e-02f, -6.19207740e-01f, 1.24386415e-01f, -7.34804422e-02f, 3.42220776e-02f, -2.27604955e-01f, 1.14511758e-01f, 2.03517992e-02f, -2.53156153e-03f, 3.28310281e-01f, 8.24824646e-02f, 2.09507927e-01f, 4.53369133e-02f, 3.65476087e-02f, -1.29297957e-01f},
    {-4.16868895e-01f, 5.95700741e-02f, 9.19658303e-01f, -4.91181284e-01f, 1.96347311e-01f, 1.75529569e-01f, -5.45578934e-02f, 7.52952090e-03f, -2.49373779e-01f, 1.66197687e-01f, -5.31845614e-02f, 9.82091054e-02f, -4.96103875e-02f, 9.61648747e-02f, 6.62254021e-02f, -1.31455705e-01f, 5.41218758e-01f, -6.92031160e-02f},
    {-2.41333805e-02f, 1.36219412e-01f, -9.19191632e-03f, 1.87424526e-01f, 5.05327806e-02f, 1.54604586e-02f, -1.27169052e-02f, -8.28533471e-02f, -3.54601234e-01f, 4.73347485e-01f, 5.50183654e-01f, -3.34267281e-02f, 4.72029522e-02f, 4.05622348e-02f, 2.58151412e-01f, 3.41599621e-02f, 1.71444956e-02f, 2.61481762e-01f},
    {-4.10056636e-02f, 1.30878106e-01f, 3.66561085e-01f, -1.27918925e-02f, 7.75731325e-01f, -4.73467648e-01f, 6.77069742e-03f, -1.78784300e-02f, 2.86720961e-01f, 7.09956348e-01f, 5.10645211e-02f, -1.30348861e-01f, -1.86423644e-01f, 1.07878961e-01f, -6.19033016e-02f, -1.79629073e-01f, -8.75633880e-02f, -1.00893438e-01f},
    {-2.14826256e-01f, -2.97678173e-01f, -6.39840290e-02f, -3.62744540e-01f, -3.70945156e-01f, -1.87150598e-01f, -4.19997387e-02f, 1.67242866e-02f, -1.26078904e-01f, 9.87530947e-01f, -1.88005179e-01f, 2.13098362e-01f, 4.72046584e-02f, -3.39379571e-02f, -1.16291113e-01f, -1.48874342e-01f, 9.44135115e-02f, 1.14750907e-01f},
    {1.91078663e-01f, -3.35103163e-04f, 1.44063577e-01f, -2.54601538e-01f, -6.32058501e-01f, -3.24034840e-02f, 7.57136121e-02f, -1.73711926e-01f, 1.57622233e-01f, -1.97055236e-01f, 1.35536268e-01f, -2.26641133e-01f, 3.55995119e-01f, -9.09744799e-02f, 2.84071028e-01f, 3.20451826e-01f, 4.26474921e-02f, 2.13984028e-01f},
    {1.28121793e-01f, -8.76373112e-01f, 2.06786189e-02f, 7.20863461e-01f, -9.04950872e-02f, 5.95078230e-01f, 2.16407385e-02f, 2.42717937e-02f, -9.44042578e-02f, -7.31561333e-02f, -4.14117090e-02f, 1.17956825e-01f, -1.45652086e-01f, -3.32660489e-02f, 1.26427328e-02f, -1.09370299e-01f, 5.12754023e-01f, -3.21312509e-02f},
    {4.71270293e-01f, 8.59382302e-02f, -3.89878839e-01f, 5.43988109e-01f, 2.44540367e-02f, -2.55439989e-02f, -6.51042024e-03f, 1.81797873e-02f, 4.30406667e-02f, -3.09936702e-01f, 3.15634161e-01f, -3.12128775e-02f, 7.80628696e-02f, -1.01477467e-01f, 2.33641230e-02f, 1.08318582e-01f, 4.02021706e-01f, -6.32633492e-02f},
    {7.10378230e-01f, -2.45592386e-01f, -2.62157828e-01f, -6.58690231e-03f, -7.76529908e-02f, 2.77550936e-01f, -5.30137718e-02f, -2.09728256e-02f, 7.41427094e-02f, 1.00375032e+00f, -1.91448092e-01f, 1.45390451e-01f, 9.58833378e-03f, 1.78213380e-02f, 1.19900163e-02f, -6.56424388e-02f, -3.57122928e-01f, -8.12053606e-02f},
    {3.03031541e-02f, 2.22182795e-02f, 1.49368420e-01f, 1.49386570e-01f, -2.97182381e-01f, -5.14340758e-01f, -3.73671041e-03f, 1.55252576e-01f, -1.08065403e+00f, -8.83402526e-01f, -9.05123472e-01f, 7.67268986e-02f, -1.21893860e-01f, 5.34930713e-02f, 8.00670907e-02f, -6.20940812e-02f, 2.28510853e-02f, 1.91371012e-02f},
    {-3.78993332e-01f, -4.40196812e-01f, 3.42991203e-01f, 9.88611951e-02f, -8.59717950e-02f, -6.65626600e-02f, -1.09845996e-01f, 2.60650277e-01f, 2.58088171e-01f, -5.56894839e-02f, -3.35235596e-01f, 2.72421747e-01f, -1.55784234e-01f, 4.72513847e-02f, 1.57546252e-01f, -8.56195018e-02f, 1.99624509e-01f, -4.36626337e-02f},
    {1.94245487e-01f, 5.12200832e-01f, -9.86526236e-02f, 7.20070675e-02f, -4.01069224e-01f, -2.52232075e-01f, 1.49602845e-01f, -1.21111467e-01f, 4.39282179e-01f, 1.93168685e-01f, -4.14743274e-01f, 1.15373306e-01f, -1.95672717e-02f, -3.87898320e-03f, 3.84258926e-02f, -1.92688461e-02f, -2.39397451e-01f, 1.05492420e-01f},
    {2.28487551e-01f, 1.68688729e-01f, 1.21841900e-01f, 6.23615980e-02f, -4.82925892e-01f, -1.89753383e-01f, 8.27984810e-02f, 5.83843477e-02f, 5.73009491e-01f, 4.75858688e-01f, 2.70811766e-01f, -2.66852051e-01f, -8.63514543e-01f, -6.72544166e-02f, -3.74189079e-01f, -4.36104499e-02f, 6.11577630e-01f, -8.43758062e-02f},
    {-6.65502483e-03f, 4.33409452e-01f, 9.56072733e-02f, -1.03075609e-01f, -6.75300732e-02f, 1.39181241e-01f, -1.90252036e-01f, -9.81437694e-03f, -1.09775126e+00f, -1.41829669e-01f, 1.56695932e-01f, 3.64917070e-01f, -1.35105267e-01f, -7.42136389e-02f, 1.07820049e-01f, 4.21657339e-02f, 2.25634485e-01f, -8.07321072e-03f},
    {2.48771787e-01f, 4.10638154e-02f, 1.86099544e-01f, -7.75795355e-02f, -7.55816281e-01f, 4.45948005e-01f, -3.94281819e-02f, -5.84723242e-02f, -9.70099270e-01f, -4.50478345e-01f, -4.67992187e-01f, 9.52481255e-02f, -3.54517885e-02f, 3.02826911e-02f, 2.26793215e-02f, -4.99572270e-02f, 2.88669467e-01f, -3.63399275e-02f},
    {8.71839747e-02f, 1.40110224e-01f, -3.51535268e-02f, -3.66156287e-02f, -1.82969779e-01f, -4.52987254e-01f, -1.03414856e-01f, -1.17138609e-01f, -3.10990006e-01f, -5.86729944e-01f, -4.87074763e-01f, 1.63841859e-01f, -3.60976346e-02f, -2.25162208e-02f, -2.74766684e-01f, 8.05211961e-02f, 1.14693865e-01f, 2.60476470e-01f},
    {-2.14201078e-01f, 7.70784467e-02f, -7.39554316e-02f, -1.05633168e-02f, 4.40467805e-01f, -5.65298572e-02f, -2.34723208e-03f, -2.48758979e-02f, -7.01430719e-03f, -1.08803138e-01f, 3.52912515e-01f, -5.33604085e-01f, -3.91757756e-01f, 2.50213295e-01f, -7.74641111e-02f, 4.88446876e-02f, -8.29941213e-01f, 3.15468371e-01f},
    {3.83613050e-01f, 2.22595841e-01f, 2.58614779e-01f, 2.44389012e-01f, 2.74460793e-01f, 1.15720935e-01f, -2.63977945e-02f, 4.80551831e-02f, 3.72973800e-01f, 1.78647816e-01f, -2.00869635e-01f, 1.19989753e-01f, -3.64768833e-01f, -3.02366093e-02f, -4.72777784e-02f, -1.24431752e-01f, -1.54445231e-01f, 5.08543514e-02f}
};
constexpr float hiddenBias[32] = {-2.30516717e-02f, -1.57205731e-01f, -2.70528376e-01f, 2.70893961e-01f, -4.23665904e-03f, 1.81930229e-01f, 2.06791237e-01f, 1.66331641e-02f, -3.61741483e-01f, -5.61992705e-01f, 1.65607765e-01f, 1.38794675e-01f, 1.13698572e-01f, -3.36731672e-01f, 2.31658682e-01f, -3.97111714e-01f, 1.21038996e-01f, -2.37714112e-01f, -4.12711352e-01f, 1.65690079e-01f, -4.70281631e-01f, 3.05126160e-01f, 1.93475172e-01f, 5.41742921e-01f, 2.85279125e-01f, -8.98681730e-02f, -3.98853749e-01f, 9.58392844e-02f, 3.65877301e-01f, 3.25638205e-01f, -5.15245855e-01f, -5.18564820e-01f};
constexpr float moveWeights[2][32] = {
    {-1.68101694e-02f, 2.01646313e-01f, -2.36327704e-02f, -1.92320630e-01f, -3.49722914e-02f, -4.21150103e-02f, 3.82617936e-02f, -9.61759165e-02f, -3.63306522e-01f, -2.99481340e-02f, 1.27387360e-01f, -4.29855436e-02f, -2.44547099e-01f, 4.85607572e-02f, -2.45471880e-01f, -3.88329387e-01f, -2.45737620e-02f, 1.72018349e-01f, -8.05925280e-02f, 1.48297682e-01f, 1.27618194e-01f, 1.50047585e-01f, 2.13599816e-01f, 1.25868738e-01f, -8.64017829e-02f, 1.90902129e-01f, 5.35586238e-01f, 1.72519043e-01f, -1.42269537e-01f, -5.03871478e-02f, 7.10947096e-01f, 1.53155867e-02f},
    {-4.72793430e-01f, 3.52535583e-02f, -2.66563296e-01f, -1.20833321e-02f, -1.48335263e-01f, 5.78476846e-01f, 2.06493318e-01f, 1.68916285e-01f, -3.07425335e-02f, 4.16583449e-01f, 7.87503645e-02f, 2.22378850e-01f, 4.85546477e-02f, 7.63560236e-01f, -3.88438217e-02f, 3.36479455e-01f, 2.26344377e-01f, -2.23252475e-01f, -3.66536200e-01f, -1.66713610e-01f, -8.68142009e-01f, 9.37571973e-02f, -1.23044260e-01f, -6.46342993e-01f, -1.79167926e-01f, 1.28569543e-01f, 4.65003371e-01f, -6.80850625e-01f, 7.70623267e-01f, 1.73275605e-01f, -1.56258307e-02f, 3.11742872e-01f}
};
constexpr float moveBias[2] = {-3.98736522e-02f, -8.96862745e-02f};

struct Segment {
    Vec from, to;
    bool active = false;
};

struct WorldMap {
    Map map;
    std::vector<int> walls;
    std::vector<float> core;
    std::vector<unsigned char> legal;

    bool wallBox(int x0, int y0, int x1, int y1) const {
        const int width = map.nx + 1;
        x0 = std::clamp(x0, 0, map.nx); x1 = std::clamp(x1 + 1, 0, map.nx);
        y0 = std::clamp(y0, 0, map.ny); y1 = std::clamp(y1 + 1, 0, map.ny);
        return walls[y1 * width + x1] - walls[y1 * width + x0] -
               walls[y0 * width + x1] + walls[y0 * width + x0] > 0;
    }
};

struct Track {
    Player last{};
    int seen = -1000;
    Vec trend{}, learned{};
    std::array<double, Models> error{};
    std::array<std::array<double, Models>, 4> contextError{};
    std::array<double, Models> weight{};
    std::array<double, 5> aimError{};
    std::array<std::array<Player, Forecast + 1>, Models> path{};
    std::array<std::array<Vec, Models>, History> past{};
    std::array<int, History> stamp{};
    std::array<int, History> context{};
    bool present = false;
    bool visible = false;
    bool armed = false;
    int readyAt = 0;
    std::array<double, Forecast + MAX_DELAY + 1> groundPickup{};
    int clashTick = -1000;
    int blockTick = -1000;
    Vec impact{};
    double reach = 0;
};

struct Hazard {
    int owner = -1;
    int launch = -1000;
    double weight = 0;
    bool observed = false;
    std::array<Segment, Horizon> ray{};
};

struct Route {
    Vec move{}, follow{}, aim{};
    int kind = A_NONE;
    int switchAt = Horizon;
    int swingAt = -1;
};

struct RoutePath {
    std::array<Body, Horizon + 1> body{};
    int hit = -1;
    bool fell = false;
    bool picked = false;
    bool armedSwing = false;
};

struct Exchange {
    std::array<Segment, Horizon> projectile{};
    std::array<std::array<double, Horizon + 1>, MAXP> incapacitated{};
    std::array<std::array<double, Horizon>, MAXP> clashBefore{};
    bool hasClash = false;
    int catchAt = Horizon + 1;
};

struct Shot {
    Vec move{}, aim{};
    double value = 0;
};

struct Bot {
    const GameInfo* gi = nullptr;
    const Config* cf = nullptr;
    const Obs* obs = nullptr;
    std::vector<WorldMap> maps;
    std::array<Vec, Angles> dirs{};
    std::array<Track, MAXP> enemy{};
    std::array<Player, History> self{};
    std::array<Dart, History> own{};
    std::array<Action, History> moves{};
    std::vector<Hazard> hazards;
    std::vector<float> flow;
    int flowPhase = -1, flowTarget = -1, flowTime = -1000;
    int nx = 0, ny = 0, now = 0, seat = 0, live = 0;
    Vec here{}, goal{}, carry{};
    Vec routeGuide{};
    bool hasGuide = false;
    double closest = Infinity;
    int focus = -1;
    bool armed = false;
    bool armedPressure = false;
    double hitRadius = 0;

    std::array<Vec, 5> aimTargets(Vec origin, int view, Vec fallback) const {
        const Player& p = self[imax(0, view) % History];
        const Vec start = point(p), v = speed(p);
        const double flight = (start - origin).len() / (0.82 * cf->dart_v);
        const double lead = cf->delay + cf->throw_pre + flight;
        Player extrapolated = p;
        Vec keep = (p.st == S_FREE || p.st == S_THROW || p.st == S_THROWN) ?
            v * (1000. / (p.st == S_FREE ? cf->run_v : cf->run_v * cf->throw_move / 100.)) : fallback;
        for (int t = 0; t < int(lead); ++t) {
            advance(extrapolated, keep, view + t);
        }
        Vec trend = (start - historicalSelf(view - 3)) * (1. / 3);
        return {start, start + v * lead, start + v * (lead * 0.55),
            point(extrapolated), start + trend * lead};
    }

    int phase(int tick) const {
        if (tick < cf->col_start) return 0;
        return imin(int(maps.size()) - 1, 1 + (tick - cf->col_start) / cf->col_every);
    }

    const WorldMap& field(int t) const { return maps[phase(t)]; }
    int id(int x, int y) const { return y * nx + x; }
    int id(Vec p) const {
        int x = std::clamp(int(roundInt(p.x / Grid)), 0, nx - 1);
        int y = std::clamp(int(roundInt(p.y / Grid)), 0, ny - 1);
        return id(x, y);
    }
    Vec gridPoint(int i) const { return {double((i % nx) * Grid), double((i / nx) * Grid)}; }

    bool pit(Vec p, int t) const {
        return cell_of_point(*cf, field(t).map, roundInt(p.x), roundInt(p.y), -1) == C_PIT;
    }
    bool wall(Vec a, Vec b, int t) const {
        const WorldMap& f = field(t);
        const i64 x0 = roundInt(a.x), y0 = roundInt(a.y), x1 = roundInt(b.x), y1 = roundInt(b.y);
        return f.wallBox(cell_index(imin(x0, x1) - 1), cell_index(imin(y0, y1) - 1),
                         cell_index(imax(x0, x1)), cell_index(imax(y0, y1))) &&
               wall_between(f.map, x0, y0, x1, y1);
    }
    bool walkable(Vec a, Vec b, int t) const {
        Vec delta = b - a;
        for (int n = 1, steps = int(delta.len() / Grid) + 1; n <= steps; ++n) {
            Vec p = a + delta * (double(n) / steps);
            if (pit(p, t) || circle_hits_wall(field(t).map, cf->body_r, roundInt(p.x), roundInt(p.y))) return false;
        }
        return true;
    }

    void distanceField(const WorldMap& f, const std::vector<int>& sources, std::vector<float>& dest) const {
        using Node = std::pair<float, int>;
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> queue;
        dest.assign(nx * ny, 10000);
        for (int i : sources) {
            if (f.legal[i]) {
                dest[i] = 0;
                queue.emplace(0, i);
            }
        }
        while (!queue.empty()) {
            auto [dist, u] = queue.top();
            queue.pop();
            if (dist != dest[u]) continue;
            int x = u % nx, y = u / nx;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                if ((!dx && !dy) || x + dx < 0 || x + dx >= nx || y + dy < 0 || y + dy >= ny) continue;
                int v = u + dy * nx + dx;
                if (!f.legal[v] || (dx && dy && (!f.legal[u + dx] || !f.legal[u + dy * nx]))) continue;
                float nd = dist + (dx && dy ? 1.414214f : 1.f);
                if (nd >= dest[v]) continue;
                dest[v] = nd;
                queue.emplace(nd, v);
            }
        }
    }

    void init(const GameInfo& game) {
        gi = &game;
        cf = &game.cfg;
        seat = game.seat;
        nx = int(game.M().width() / Grid) + 1;
        ny = int(game.M().height() / Grid) + 1;
        hitRadius = cf->body_r + cf->dart_r;
        int last = 0;
        for (int y = 0; y < game.M().ny; ++y) for (int x = 0; x < game.M().nx; ++x)
            last = imax(last, 1 + game.M().wave(x, y));
        maps.reserve(last + 1);
        for (int p = 0; p <= last; ++p) {
            WorldMap f;
            f.map = live_map(*cf, game.M(), p == 0 ? -1 : cf->col_start + (p - 1) * cf->col_every);
            const int w = f.map.nx + 1, h = f.map.ny + 1;
            f.walls.assign(w * h, 0);
            for (int y = 1; y < h; ++y) for (int x = 1; x < w; ++x) {
                int i = y * w + x;
                f.walls[i] = f.walls[i - 1] + f.walls[i - w] - f.walls[i - w - 1] + (f.map.at(x - 1, y - 1) == C_WALL);
            }
            f.legal.resize(nx * ny);
            for (int i = 0; i < nx * ny; ++i) {
                Vec q = gridPoint(i);
                f.legal[i] = cell_of_point(*cf, f.map, roundInt(q.x), roundInt(q.y), -1) != C_PIT &&
                    !circle_hits_wall(f.map, cf->body_r, roundInt(q.x), roundInt(q.y));
            }
            maps.push_back(std::move(f));
        }
        std::vector<int> core;
        for (int i = 0; i < nx * ny; ++i) if (maps.back().legal[i]) core.push_back(i);
        for (WorldMap& f : maps) distanceField(f, core, f.core);
        for (int k = 0; k < Angles; ++k) {
            double a = k * 6.283185307179586 / Angles;
            dirs[k] = {1000 * std::cos(a), 1000 * std::sin(a)};
        }
        for (auto& e : enemy) e.stamp.fill(-1000);
        hazards.reserve(128);
    }

    template<class Pose>
    void bodyMove(Pose& p, int t) const {
        const WorldMap& f = field(t);
        i64 r = cf->body_r, x = p.x + p.vx, y = p.y + p.vy;
        if (f.wallBox(cell_index(imin(x, p.x) - r), cell_index(imin(y, p.y) - r),
                      cell_index(imax(x, p.x) + r), cell_index(imax(y, p.y) + r))) {
            move_body(f.map, r, p.x, p.y, p.vx, p.vy);
        } else {
            p.x = x; p.y = y;
        }
    }

    template<class Pose>
    bool dashMoves(const Pose& p) const {
        return p.st == S_DASH && cf->dash_post < p.timer && p.timer <= cf->dash_post + cf->dash_t;
    }

    template<class Pose>
    void begin(Pose& p, int kind, Vec aim, bool hasDart, Vec move) const {
        if (p.st == S_RECALL && (kind == A_THROW || kind == A_DASH || kind == A_MELEE || move.sq() > 0)) {
            p.st = S_FREE; p.timer = 0;
        }
        if (kind == A_MELEE && cf->dash_slash && dashMoves(p)) {
            p.st = S_FREE; p.timer = 0;
        }
        if (p.st != S_FREE || kind == A_NONE) return;
        scale_to(roundInt(aim.x), roundInt(aim.y), UNIT, p.ax, p.ay);
        if (kind == A_DASH) {
            p.st = S_DASH; p.timer = cf->dash_pre + cf->dash_t + cf->dash_post; p.cd = cf->dash_cd;
        } else if (kind == A_MELEE) {
            p.st = hasDart ? S_SLASH : S_KICK; p.timer = hasDart ? cf->slash_pre : cf->kick_pre;
        } else if (kind == A_THROW) {
            p.st = S_THROW; p.timer = cf->throw_pre;
        } else {
            p.st = S_RECALL; p.timer = cf->recall_pre;
        }
    }

    template<class Pose>
    bool advance(Pose& p, Vec move, int t) const {
        const bool dashing = dashMoves(p);
        bool overPit = dashing && p.timer > cf->dash_post + 1;
        if (dashing) {
            p.vx = rdiv(p.ax * cf->dash_v, UNIT); p.vy = rdiv(p.ay * cf->dash_v, UNIT);
            bodyMove(p, t);
            if (!overPit) {
                p.vx = rdiv(p.vx * cf->run_v, cf->dash_v); p.vy = rdiv(p.vy * cf->run_v, cf->dash_v);
            }
        } else if (p.st == S_STUN) {
            overPit = p.timer > 1;
            bodyMove(p, t);
            p.vx = rdiv(p.vx * cf->kick_keep, 1000); p.vy = rdiv(p.vy * cf->kick_keep, 1000);
        } else {
            i64 percent = p.st == S_FREE ? 100 : (p.st == S_THROW || p.st == S_THROWN ? cf->throw_move : 0);
            i64 mx = roundInt(move.x), my = roundInt(move.y), vx = 0, vy = 0;
            if (percent && (mx || my)) {
                i64 requested = imin(ilen(mx, my), UNIT);
                scale_to(mx, my, rdiv(cf->run_v * percent * requested, 100 * UNIT), vx, vy);
            }
            vx -= p.vx; vy -= p.vy;
            cap_len(vx, vy, cf->acc);
            p.vx += vx; p.vy += vy;
            bodyMove(p, t);
        }
        if (p.cd > 0) --p.cd;
        if (p.st == S_FREE) {
            if (p.timer > 0) --p.timer;
        } else if (--p.timer <= 0) {
            switch (p.st) {
                case S_THROW: p.st = S_THROWN; p.timer = cf->throw_post; break;
                case S_SLASH: p.st = S_SLASHED; p.timer = cf->slash_post; break;
                case S_KICK: p.st = S_KICKED; p.timer = cf->kick_post; break;
                case S_STUN: p.st = S_FREE; p.timer = 1; break;
                default: p.st = S_FREE; p.timer = 0; break;
            }
        }
        return overPit || !pit(point(p), t);
    }

    void enemyStep(Player& p, Vec move, int t) const {
        Player next = p;
        if (advance(next, move, t)) { p = next; return; }
        if (p.st != S_FREE && p.st != S_THROW && p.st != S_THROWN) { p = next; return; }
        // A free opponent is unlikely to knowingly run into a hole.
        for (Vec alt : {Vec{move.x, 0}, Vec{0, move.y}, Vec{}}) {
            next = p;
            if (advance(next, alt, t)) { p = next; return; }
        }
        p = next;
    }

    Vec historicalSelf(int t) const { return t >= now ? here : point(self[imax(0, t) % History]); }

    Vec intent(int m, int k, const Player& p, Vec base, int t) const {
        const auto& e = enemy[k];
        Vec delta = historicalSelf(t) - point(p);
        if (m == 0) return base;
        if (m == 1) return e.trend;
        if (m == 7) return e.learned;
        if (m == 2) return {};
        if (m == 3) return delta.unit(1000);
        if (m == 4) {
            if (obs->d[k].st == D_GROUND) return (point(obs->d[k]) - point(p)).unit(1000);
            return delta.unit(-1000);
        }
        Vec side = delta.left().unit(m == 5 ? 1000 : -1000);
        return (side + base * 0.23).unit(1000);
    }

    int motionContext(const Player& p, const Dart& d) const {
        if (p.st != S_FREE && p.st != S_THROWN) return 3;
        if (d.st == D_GROUND || d.st == D_RISE) return 2;
        if (d.st == D_HAND && (point(p) - here).sq() < 4500. * 4500.) return 1;
        return 0;
    }


    Vec learnedIntent(const Player& p, const Track& e, int k, int ref) const {
        const Player& viewer = self[ref % History];
        const Vec offset = point(viewer) - point(p);
        if (p.st != S_FREE || offset.sq() <= 450. * 450. || offset.sq() >= 8000. * 8000.)
            return e.trend;
        const double distance = offset.len();
        const Vec radial = offset * (1. / distance);
        const Vec side = radial.left();
        const Vec v = speed(p) * (1. / cf->run_v);
        const Vec prev = e.seen == ref - 1 && e.last.st == S_FREE
            ? speed(e.last) * (1. / cf->run_v) : v;
        const Vec trend = e.trend * 0.001;
        const Vec viewerV = speed(viewer) * (1. / cf->run_v);
        const float features[] = {
            float(v.dot(radial)), float(v.dot(side)),
            float(prev.dot(radial)), float(prev.dot(side)),
            float(trend.dot(radial)), float(trend.dot(side)),
            float(viewerV.dot(radial)), float(viewerV.dot(side)),
            float(std::min(1.40, distance / 6500.)), float(p.cd / double(std::max<i64>(1, cf->dash_cd))),
            float(obs->d[k].st == D_HAND), float(own[ref % History].st == D_HAND),
            float(cf->mode == M_FFA), float(p.expose > 0),
            float(dart_flying(own[ref % History].st)), float(ref >= cf->col_start - 20),
            float(p.timer > 0), float(viewer.st == S_FREE),
        };
        float output[] = {moveBias[0], moveBias[1]};
        for (int m = 0; m < 32; ++m) {
            float hidden = hiddenBias[m];
            for (int f = 0; f < 18; ++f) hidden += features[f] * hiddenWeights[m][f];
            const float activation = std::max(0.f, hidden);
            output[0] += activation * moveWeights[0][m];
            output[1] += activation * moveWeights[1][m];
        }
        const Vec motion = radial * output[0] + side * output[1];
        return motion.sq() > 1. ? motion.unit(1000) : motion * 1000.;
    }

    void track() {
        const int ref = imax(0, now - cf->delay);
        const auto lag = now - ref;
        closest = Infinity; focus = -1; live = 0; armedPressure = false;
        for (int i = 1; i < obs->n; ++i) {
            int k = (seat + i) % obs->n;
            auto& e = enemy[k];
            const Player& seen = obs->p[k];
            e.present = seen.alive && !is_mate(*gi, k);
            e.visible = e.present && seen.vis;
            e.armed = obs->d[k].st == D_HAND;
            if (e.present) ++live;
            if (!seen.alive && !dart_flying(obs->d[k].st)) continue;
            if (seen.vis && e.seen != ref) {
                if (seen.st == S_THROW && (e.last.st != S_THROW || e.seen != ref - 1)) {
                    int when = imax(0, ref - (cf->throw_pre - seen.timer));
                    auto aimed = aimTargets(point(seen), imax(0, when - cf->delay), {});
                    Vec actual = heading(seen).unit();
                    for (int a = 0; a < int(aimed.size()); ++a) {
                        double error = 1 - actual.dot((aimed[a] - point(seen)).unit());
                        e.aimError[a] = 0.78 * e.aimError[a] + 0.22 * std::min(error, 0.7);
                    }
                }
                int old = (ref + History - 5) % History;
                if (e.stamp[old] == ref - 5 && seen.st == S_FREE) for (int m = 0; m < Models; ++m) {
                    double miss = (point(seen) - e.past[old][m]).sq() / 900000;
                    e.error[m] = 0.86 * e.error[m] + 0.14 * std::min(miss, 5.);
                    auto& score = e.contextError[e.context[old]][m];
                    score = 0.85 * score + 0.15 * std::min(miss, 5.);
                }
                const Vec measured = speed(seen) * (1000. / cf->run_v);
                if (seen.st == S_FREE) e.trend = (ref - e.seen == 1 ? e.trend * 0.57 + measured * 0.43 : measured);
                e.learned = learnedIntent(seen, e, k, ref);
                e.last = seen; e.seen = ref;
            }
            Player basePlayer = seen.vis ? seen : e.last;
            if (e.seen < 0 || (!seen.vis && ref - e.seen > 24)) {
                basePlayer.x = gi->spx[k]; basePlayer.y = gi->spy[k];
                if (dart_flying(obs->d[k].st)) {
                    basePlayer.x = obs->d[k].x; basePlayer.y = obs->d[k].y;
                }
                basePlayer.vx = 0; basePlayer.vy = 0; basePlayer.st = S_FREE; basePlayer.timer = 0;
            }
            Vec base = speed(basePlayer) * (1000. / cf->run_v);
            if (basePlayer.st == S_THROW || basePlayer.st == S_THROWN)
                base = base * (100. / cf->throw_move);
            if (basePlayer.st == S_DASH) base = heading(basePlayer);
            if (!seen.alive) base = {};
            int unseen = seen.vis ? 0 : std::clamp(ref - e.seen, 0, 10);
            double sum = 0;
            const double prior[Models] = {3.7, 2.1, 1.12, 1.05, 0.95, 0.95, 0.95, 1.65};
            const int context = motionContext(basePlayer, obs->d[k]);
            const bool ground = e.visible && obs->d[k].st == D_GROUND;
            const Vec drop = point(obs->d[k]);
            e.groundPickup.fill(0.);
            for (int m = 0; m < Models; ++m) {
                Player p = basePlayer;
                int pickup = Forecast + 1;
                const auto checkPickup = [&](int t) {
                    // Pickup follows movement. Only the next input can use the dart.
                    if (ground && pickup > Forecast && (point(p) - drop).sq() <= cf->pick_r * cf->pick_r &&
                        !pit(point(p), now + t - 1)) pickup = t;
                };
                for (int j = -unseen; j < lag; ++j) {
                    int t = ref + j;
                    if (seen.alive) enemyStep(p, j < 2 && j >= 0 && m >= 3 ? base : intent(m, k, p, base, t), t);
                    if (ground) checkPickup(t + 1 - now);
                }
                e.path[m][0] = p;
                for (int j = 0; j < Forecast; ++j) {
                    if (seen.alive) enemyStep(p, intent(m, k, p, base, now + j), now + j);
                    e.path[m][j + 1] = p;
                    if (ground) checkPickup(j + 1);
                }
                const double loss = ContextualTracking ? 0.55 * e.error[m] + 0.45 * e.contextError[context][m] : e.error[m];
                double modelPrior = prior[m];
                if (ContextualTracking && context == 2 && m == 4) modelPrior *= 2.25;
                e.weight[m] = modelPrior / (0.25 + loss);
                sum += e.weight[m];
                if (pickup <= Forecast) e.groundPickup[pickup + MAX_DELAY] += e.weight[m];
                if (lag <= 5) e.past[ref % History][m] = e.path[m][5 - lag].alive ?
                    point(e.path[m][5 - lag]) : point(basePlayer);
            }
            e.stamp[ref % History] = ref;
            e.context[ref % History] = context;
            for (double& w : e.weight) w /= sum;
            if (ground) {
                // Sum mutually exclusive movement hypotheses into a possession curve.
                double cumulative = 0;
                for (double& p : e.groundPickup) {
                    cumulative += p / sum;
                    p = clip(cumulative);
                }
            }
            e.reach = e.visible ? 1 : (ref - e.seen < 7 ? 0.48 : 0.14);
            if (!e.present) continue;
            const double d = (point(e.path[0][0]) - here).len() + (e.visible ? 0 : 1600);
            armedPressure |= e.visible && d < 7600 && (e.armed || obs->d[k].st == D_BACK || obs->d[k].st == D_CALL);
            if (d < closest) { closest = d; focus = k; }
        }
    }

    void inferContact() {
        if (!ContactInference || now == 0 || closest > 1900) return;
        Player previous = self[(now - 1) % History];
        const Action& a = moves[(now - 1) % History];
        Vec mv{double(a.mx), double(a.my)};
        const Vec start = point(previous);
        begin(previous, int(a.act), {double(a.ax), double(a.ay)}, own[(now - 1) % History].st == D_HAND, mv);
        if (!advance(previous, mv, now - 1)) return;
        const Vec expected = point(previous), push = here - expected;
        const double delta = push.len();
        if (delta < 28 || delta > cf->body_r || (expected - start).sq() > cf->run_v * cf->run_v * 1.12) return;
        const auto& f = field(now);
        const int x = cell_index(roundInt(here.x)), y = cell_index(roundInt(here.y));
        if (f.wallBox(x - 1, y - 1, x + 1, y + 1)) return;
        const Vec contact = here - push.unit(2 * cf->body_r);
        int match = -1;
        for (int k = 0; k < obs->n; ++k) {
            if (!enemy[k].present || (point(enemy[k].path[0][0]) - contact).sq() > 1380. * 1380.) continue;
            if (match >= 0) return;
            match = k;
        }
        if (match < 0) return;
        auto& e = enemy[match];
        for (int m = 0; m < Models; ++m) {
            Player p = e.path[m][0];
            Vec inferred = contact * 0.82 + point(p) * 0.18;
            p.x = roundInt(inferred.x); p.y = roundInt(inferred.y);
            e.path[m][0] = p;
            Vec base = speed(p) * (1000. / cf->run_v);
            for (int j = 0; j < Forecast; ++j) {
                enemyStep(p, intent(m, match, p, base, now + j), now + j);
                e.path[m][j + 1] = p;
            }
        }
        e.reach = std::max(e.reach, 0.91);
        closest = (point(e.path[0][0]) - here).len();
    }

    Segment fly(Dart& d, Vec ownerFrom, Vec ownerTo, int t) const {
        if (!dart_flying(d.st)) return {};
        Segment s{point(d), point(d), true};
        const i64 length = dart_step_len(*cf, d);
        ++d.age;
        bool reaches = false;
        if (d.st == D_OUT) {
            i64 step = imin(length, d.left);
            d.x += rdiv(d.vx * step, UNIT); d.y += rdiv(d.vy * step, UNIT);
            d.left -= step;
        } else if (length > 0) {
            i64 dx = roundInt(ownerTo.x) - d.x, dy = roundInt(ownerTo.y) - d.y;
            i64 dist = ilen(dx, dy);
            reaches = dist <= length;
            d.x += reaches ? dx : rdiv(dx * length, dist);
            d.y += reaches ? dy : rdiv(dy * length, dist);
        }
        const auto& f = field(t);
        const i64 x0 = roundInt(s.from.x), y0 = roundInt(s.from.y);
        i64 hx, hy;
        if (!(d.st == D_CALL && cf->recall_wall) &&
            f.wallBox(cell_index(imin(x0, d.x) - 1), cell_index(imin(y0, d.y) - 1),
                      cell_index(imax(x0, d.x)), cell_index(imax(y0, d.y))) &&
            seg_wall(f.map, x0, y0, d.x, d.y, hx, hy)) {
            d.x = hx; d.y = hy; d.st = D_GROUND;
        }
        s.to = point(d);
        if (d.st == D_OUT && d.left == 0) { d.st = D_BACK; d.age = 0; }
        else if ((d.st == D_BACK || d.st == D_CALL) &&
                 (reaches || segmentDistance2(s.from - ownerFrom, s.to - ownerTo) <= cf->catch_r * cf->catch_r)) {
            d.st = D_HAND;
        }
        return s;
    }

    Dart release(Vec origin, Vec aim) const {
        Dart d;
        d.st = D_OUT; d.left = cf->range; d.x = roundInt(origin.x); d.y = roundInt(origin.y);
        scale_to(roundInt(aim.x), roundInt(aim.y), UNIT, d.vx, d.vy);
        return d;
    }

    void inferImpact() {
        if (!ImpactInference || now == 0 || obs->my_dart().st != D_GROUND) return;
        const Dart& old = own[(now - 1) % History];
        if (!dart_flying(old.st)) return;
        Dart natural = old;
        Segment s = fly(natural, historicalSelf(now - 1), here, now - 1);
        Vec impact = point(obs->my_dart());
        if (natural.st == D_GROUND && (point(natural) - impact).sq() < 16 * 16) return;
        const bool parry = (impact - s.from).sq() < 4;
        int candidate = -1;
        double best = Infinity, next = Infinity;
        for (int k = 0; k < obs->n; ++k) {
            auto& e = enemy[k];
            const Dart& ed = obs->d[k];
            if (!e.present || !e.visible || (parry && ed.st == D_GROUND)) continue;
            const Vec location = point(e.path[0][0]);
            double cost = (location - impact).len();
            if (!parry && dart_flying(ed.st)) {
                Dart advanced = ed;
                const int ref = int(imax(0, now - cf->delay));
                for (int t = ref; t < now; ++t) fly(advanced, location, location, t);
                cost = (point(advanced) - impact).len();
            }
            if (cost < best) { next = best; best = cost; candidate = k; }
            else if (cost < next) next = cost;
        }
        if (candidate < 0 || best > (parry ? 1720 : 3600) || next - best < 750) return;
        auto& e = enemy[candidate];
        e.impact = impact;
        if (parry) e.blockTick = now;
        else e.clashTick = now;
    }

    void knownThreats(int k, int ref) {
        const Player& seen = obs->p[k];
        const Dart original = obs->d[k];
        if (!(dart_flying(original.st) || original.st == D_RISE || (original.st == D_HAND && seen.st == S_THROW))) return;
        if (!seen.vis && !dart_flying(original.st)) return;
        const auto& e = enemy[k];
        enemy[k].readyAt = original.st == D_HAND && seen.st != S_THROW ? 0 : Horizon + 8;
        int branches = 3;
        for (int m = 0; m < branches; ++m) {
            int model = m == 2 ? 3 : m;
            Dart d = original;
            Player p = seen.vis ? seen : e.last;
            Vec base = speed(p) * (1000. / cf->run_v);
            if (p.st == S_THROW || p.st == S_THROWN) base = base * (100. / cf->throw_move);
            Hazard h;
            h.owner = k; h.observed = true;
            h.weight = m == 0 ? (branches == 1 ? 1. : 0.56) : 0.22;
            for (int t = ref; t < now + Horizon; ++t) {
                Vec old = point(p);
                bool fresh = p.st == S_THROW && p.timer == 1 && d.st == D_HAND;
                if (t < now) {
                    if (seen.alive && seen.vis) enemyStep(p, intent(model, k, p, base, t), t);
                } else p = e.path[model][t - now + 1];
                if (fresh) d = release(point(p), heading(seen));
                if (d.st == D_RISE && --d.left <= 0) { d.st = D_CALL; d.age = 0; }
                Segment ray = fresh ? Segment{} : fly(d, old, point(p), t);
                if (m == 0 && d.st == D_HAND && !fresh && p.st != S_THROW && !dart_flying(d.st))
                    enemy[k].readyAt = imax(0, t - now + 1);
                if (t < now && ray.active && segmentDistance2(ray.from - historicalSelf(t),
                    ray.to - historicalSelf(t + 1)) < (hitRadius - 80) * (hitRadius - 80)) {
                    // This branch cannot explain survival. A clash or block may already have happened.
                    break;
                }
                if (t >= now) h.ray[t - now] = ray;
                if (!dart_flying(d.st) && d.st != D_RISE && !fresh && !(d.st == D_HAND && p.st == S_THROW)) break;
            }
            if (std::any_of(h.ray.begin(), h.ray.end(), [](const Segment& s) { return s.active; }))
                hazards.push_back(h);
        }
    }

    void blindThreats(int k, int ref) {
        const auto& e = enemy[k];
        const bool ground = obs->d[k].st == D_GROUND;
        const bool picked = !ground && ImpactInference && e.clashTick > ref && e.readyAt < 5;
        const auto& p = picked ? e.path[0][0] : obs->p[k];
        if (!e.present || (!e.armed && !picked && !ground) || !e.visible || p.st == S_THROW ||
            (point(e.path[0][0]) - here).sq() > 7900. * 7900.) return;
        if (ImpactInference && e.blockTick > ref) return;
        const int originTime = picked ? now : ref;
        for (int start = picked ? now + e.readyAt - 1 : ref; start <= now + 1; ++start) {
            if (p.st != S_FREE && p.timer > start - ref) continue;
            if (start < now - 4) continue;
            const double held = ground ? e.groundPickup[start - now + MAX_DELAY] : 1.;
            if (held < 0.12) continue;
            const int releaseAt = start + cf->throw_pre - 1;
            Player origin = p;
            Vec m = speed(p) * (1000. / cf->run_v);
            for (int t = originTime; t <= releaseAt; ++t) {
                if (t == start) begin(origin, A_THROW, {1000, 0}, true, m);
                const Vec move = ground && t < start ? intent(4, k, origin, m, t) : m;
                enemyStep(origin, move, t);
            }
            const int view = int(imax(0, start - cf->delay));
            const Player& prior = self[view % History];
            if (prior.expose <= 0 && (point(prior) - point(p)).sq() > cf->see_r * cf->see_r &&
                cell_of_point(*cf, field(view).map, prior.x, prior.y, -1) == C_GRASS) continue;
            auto targets = aimTargets(point(origin), view, carry);
            for (int style = 0; style < int(targets.size()); ++style) {
                Vec aim = (targets[style] - point(origin)).unit(1000);
                Dart d = release(point(origin), aim);
                Hazard h;
                h.owner = k;
                h.launch = start - now;
                h.weight = held * (start < now ? 0.043 : 0.050) / (1 + 10 * e.aimError[style]);
                bool possible = true;
                for (int t = releaseAt + 1; t < now + Horizon; ++t) {
                    Segment s = fly(d, point(origin), point(origin), t);
                    if (t < now && segmentDistance2(s.from - historicalSelf(t),
                        s.to - historicalSelf(t + 1)) <= hitRadius * hitRadius) {
                        possible = false; break;
                    }
                    if (t >= now && t < now + Horizon) h.ray[t - now] = s;
                    if (d.st != D_OUT) break;
                }
                if (possible) hazards.push_back(h);
            }
        }
    }

    void crossingThreats(int k) {
        const auto& shooter = enemy[k];
        const bool ground = obs->d[k].st == D_GROUND;
        if (!(Crossfire && live > 1 && shooter.present && shooter.visible && (shooter.armed || ground))) return;
        if ((point(shooter.path[0][0]) - here).sq() > 7900. * 7900.) return;
        for (int target = 0; target < obs->n; ++target) {
            if (target == k || !enemy[target].present || !enemy[target].visible) continue;
            const auto& victim = enemy[target];
            for (int start : {-2, 1, 4}) {
                if (start < -now) continue;
                const double held = ground ? shooter.groundPickup[start + MAX_DELAY] : 1.;
                if (held < 0.12) continue;
                int releaseAt = start + int(cf->throw_pre);
                int originTick = imax(0, releaseAt);
                const Vec origin = point(shooter.path[0][originTick]);
                Vec bearing = point(victim.path[0][0]) - origin;
                const double distance = bearing.len();
                if (distance < 1250 || distance > 7900) continue;
                Vec targetPos = point(victim.path[0][std::clamp(originTick + int(distance / cf->dart_v), 0, Forecast)]);
                if (segmentDistance2(origin - here, targetPos - here) > 2800. * 2800.) continue;
                if (shooter.path[0][imax(0, start)].st != S_FREE || wall(origin, targetPos, now + originTick)) continue;
                Dart d = release(origin, (targetPos - origin).unit(1000));
                Hazard h;
                h.owner = k; h.weight = 0.09 * held;
                h.launch = start;
                for (int j = releaseAt; j < Horizon; ++j) {
                    Segment s = fly(d, origin, origin, now + j);
                    if (j >= 0) h.ray[j] = s;
                    if (d.st != D_OUT) break;
                }
                hazards.push_back(h);
            }
        }
    }

    void navigation() {
        const Dart& d = obs->my_dart();
        hasGuide = false;
        bool pickup = d.st == D_GROUND || d.st == D_RISE;
        goal = pickup ? point(d) : focus >= 0 ? point(enemy[focus].path[0][0]) :
            Vec{gi->M().width() * 0.5, gi->M().height() * 0.5};
        if (pickup && pit(goal, now + 10)) pickup = false;
        if (!pickup && (d.st == D_GROUND || d.st == D_RISE) && focus >= 0) goal = point(enemy[focus].path[0][0]);
        if ((goal - here).sq() < 40000) return;
        const int f = phase(now);
        const auto& map = maps[f];
        int target = id(goal);
        if (map.legal[target] && walkable(here, goal, now + 5)) {
            routeGuide = (goal - here).unit(1000); hasGuide = true; return;
        }
        if (goal.x < 0 || goal.y < 0 || goal.x > gi->M().width() || goal.y > gi->M().height()) return;
        if (!map.legal[target]) {
            double best = Infinity;
            int x = target % nx, y = target / nx;
            for (int dy = -4; dy <= 4; ++dy) for (int dx = -4; dx <= 4; ++dx) {
                if (x + dx < 0 || x + dx >= nx || y + dy < 0 || y + dy >= ny) continue;
                int i = id(x + dx, y + dy);
                if (!map.legal[i]) continue;
                double cost = (gridPoint(i) - goal).sq();
                if (cost < best) { best = cost; target = i; }
            }
        }
        if (flowPhase != f || (flowTarget != target && now - flowTime >= 7)) {
            distanceField(map, {target}, flow);
            flowPhase = f; flowTarget = target; flowTime = now;
        }
        int start = id(here);
        int x = start % nx, y = start / nx;
        double best = Infinity;
        for (int dy = -4; dy <= 4; ++dy) for (int dx = -4; dx <= 4; ++dx) {
            if (x + dx < 0 || x + dx >= nx || y + dy < 0 || y + dy >= ny) continue;
            int i = id(x + dx, y + dy);
            Vec q = gridPoint(i);
            if (flow[i] >= 10000 || (q - here).sq() < 105 * 105 || !walkable(here, q, now + 5)) continue;
            double score = flow[i] + (q - here).len() / (Grid * 2.1);
            if (score < best) { best = score; routeGuide = (q - here).unit(1000); hasGuide = true; }
        }
    }

    RoutePath simulate(const Route& route) const {
        RoutePath path;
        Body p(obs->me());
        path.body[0] = p;
        bool holding = armed;
        for (int j = 0; j < Horizon; ++j) {
            Vec move = j < route.switchAt ? route.move : route.follow;
            int kind = j == 0 ? route.kind : j == route.swingAt ? A_MELEE : A_NONE;
            if (route.kind == A_RECALL || (route.kind == A_NONE && obs->me().st == S_RECALL && route.move.sq() == 0))
                move = {};
            if (kind != A_NONE || p.st == S_RECALL) begin(p, kind, kind == A_DASH && route.swingAt >= 0 ? route.follow : route.aim, holding, move);
            if ((p.st == S_SLASH || p.st == S_KICK) && p.timer == 1) {
                path.hit = j; path.armedSwing = p.st == S_SLASH;
            }
            if (!advance(p, move, now + j)) { path.fell = true; return path; }
            const Dart& md = obs->my_dart();
            if (!holding && md.st == D_GROUND && p.st != S_RECALL &&
                (point(p) - point(md)).sq() <= cf->pick_r * cf->pick_r) {
                holding = true; path.picked = true;
            }
            path.body[j + 1] = p;
        }
        return path;
    }

    bool cone(Vec a, Vec b, Vec aim, double reach, double cosine, double close, int t) const {
        Vec delta = b - a;
        double ds = delta.sq();
        if (ds > reach * reach) return false;
        if (ds > close * close && aim.dot(delta) < cosine * std::sqrt(ds)) return false;
        return !wall(a, b, t);
    }

    Vec kickedLanding(Vec victim, Vec source, int t) const {
        Player p;
        p.x = roundInt(victim.x); p.y = roundInt(victim.y);
        Vec v = (victim - source).unit(cf->kick_v);
        p.vx = roundInt(v.x); p.vy = roundInt(v.y);
        p.st = S_STUN; p.timer = cf->kick_stun;
        for (int j = 1; j <= cf->kick_stun; ++j) advance(p, {}, t + j);
        return point(p);
    }

    double melee(const RoutePath& path, Vec aim) const {
        const int j = path.hit;
        if (j < 0) return 0;
        const bool slash = path.armedSwing;
        const Vec a = point(path.body[j + 1]);
        const double reach = slash ? cf->slash_reach : cf->kick_reach;
        double value = 0;
        for (int k = 0; k < obs->n; ++k) {
            const auto& e = enemy[k];
            if (!e.present || (point(e.path[0][0]) - a).sq() > (reach + 3100.) * (reach + 3100.)) continue;
            double hit = 0, land = 0, advantage = 0;
            for (int m = 0; m < Models; ++m) {
                const Player& q = e.path[m][j + 1];
                const Player& before = e.path[m][j];
                if (!slash && (before.st == S_STUN || (before.st == S_FREE && before.timer > 0))) continue;
                if (!cone(a, point(q), aim, reach, (slash ? cf->slash_cos : cf->kick_cos) / 1000.,
                          cf->body_r, now + j)) continue;
                hit += e.weight[m];
                if (slash) continue;
                Vec p = kickedLanding(point(q), a, now + j);
                land += e.weight[m] * (pit(p, now + j + cf->kick_stun) ? 1 : 0);
                if (before.st == S_RECALL || before.st == S_THROW || before.st == S_DASH)
                    advantage += e.weight[m];
            }
            if (slash) value += 10.4 * hit * e.reach;
            else value += (8.5 * land + 0.7 * hit + 1.2 * advantage) * e.reach;
        }
        return value / (1 + 0.13 * j);
    }

    double position(Vec q, int j, bool withDart) const {
        const WorldMap& map = field(now + j);
        const int i = id(q);
        double future = field(now + j + 12).legal[i] ? 0 : 1.3;
        const int fall = cell_fall_step(*gi, cell_index(roundInt(q.x)), cell_index(roundInt(q.y)));
        if (fall >= 0) {
            double slack = fall - now - j;
            if (slack < 45) future += (45 - slack) * 0.027;
        }
        double distance = map.core[i] < 10000 ? map.core[i] * Grid : 10500;
        double coreWeight = now < 180 ? 0.065 + 0.0012 * now : 0.31;
        if (now + j > cf->col_start) coreWeight += 0.09;
        if (live > 1) coreWeight *= 1.45;
        double value = -coreWeight * distance / 1000 - future;
        double nearest = Infinity, second = Infinity;
        double surround = 0;
        Vec threats{};
        for (int k = 0; k < obs->n; ++k) {
            if (!enemy[k].present) continue;
            const Vec delta = point(enemy[k].path[0][j]) - q;
            double d = delta.len();
            if (d < nearest) { second = nearest; nearest = d; }
            else if (d < second) second = d;
            if (live > 1 && enemy[k].readyAt < j + 5 && d < 6500 &&
                !wall(q, point(enemy[k].path[0][j]), now + j)) {
                double w = clip((6500 - d) / 3300);
                threats = threats + delta.unit(w);
                surround += w;
            }
        }
        if (focus >= 0) {
            const auto& e = enemy[focus];
            const bool enemyReady = e.armed || (WeaponClock && e.readyAt < j + 5);
            double desired = withDart ? (enemyReady ? (live > 1 ? 5100 : 3850) : 1900) : (live > 1 ? 5600 : 4150);
            const double d = (q - point(e.path[0][j])).len();
            double farCost = withDart ? (enemyReady ? 0.30 : 0.64) : 0.08;
            if (live > 1) {
                desired += withDart ? (enemyReady ? 700 : 1500) : 1100;
                farCost *= 0.58;
            }
            value -= std::max(0., d - desired) * farCost / 1000;
            value -= std::max(0., desired - d) * (withDart && !enemyReady ? 0.10 : 0.41) / 1000;
            if (withDart && d < 6500 && wall(q, point(e.path[0][j]), now + j)) value -= 0.29;
            const Dart& ed = obs->d[focus];
            if (live == 1 && withDart && (ed.st == D_GROUND || ed.st == D_RISE) && d < 7500) {
                double guard = (q - point(ed)).len();
                double theirs = (point(e.path[0][j]) - point(ed)).len();
                if (theirs > 1800 && guard < theirs + 600)
                    value += WeaponDenial * (0.75 * clip((4100 - guard) / 2700.) +
                        0.44 * clip((theirs - guard) / 2100.));
            }
        }
        if (live > 1 && second < 5200) value -= FfaSeparation * (5200 - second) / 1130;
        value -= FfaSeparation * (surround - threats.len()) * 2.2;
        if (live > 1 && nearest > cf->see_r + 150) {
            const int cover = cell_of_point(*cf, map.map, roundInt(q.x), roundInt(q.y), -1);
            if (cover == C_GRASS) value += 0.35;
        }
        const auto& d = obs->my_dart();
        if (!withDart && (d.st == D_GROUND || d.st == D_RISE) && !pit(point(d), now + j + 4)) {
            value -= 0.75 * std::min((q - point(d)).len() / 1000, 9.);
            if (flowPhase == phase(now) && flowTarget >= 0 && (gridPoint(flowTarget) - point(d)).sq() < CELL * CELL)
                value -= 0.54 * std::min(double(flow[i] * Grid / 1000), 11.);
        }
        if (hasGuide && (withDart || d.st == D_GROUND || d.st == D_RISE) &&
            (goal - here).sq() > 2500. * 2500.) {
            value += 0.29 * (q - here).dot(routeGuide) / 1000000;
        }
        if (obs->me().expose <= j && cell_of_point(*cf, map.map, roundInt(q.x), roundInt(q.y), -1) == C_GRASS &&
            nearest > cf->see_r + 250) value += withDart ? 0.40 : 0.26;
        if (nearest < 4500) {
            for (Vec delta : {Vec{270, 0}, Vec{-270, 0}, Vec{0, 270}, Vec{0, -270}})
                if (pit(q + delta, now + j)) value -= 0.18;
        }
        return value;
    }

    double clashPoint(const Segment& own, const Segment& hostile) const {
        if (!own.active) return -1;
        const Vec start = own.from - hostile.from;
        const Vec finish = own.to - hostile.to;
        const Vec velocity = finish - start;
        const double radius = 2 * cf->dart_r;
        if (start.sq() <= radius * radius || velocity.sq() <= 0.01) return -1;
        const double at = clip(-start.dot(velocity) / velocity.sq());
        return (start + velocity * at).sq() <= radius * radius ? at : -1;
    }

    bool blocksAt(const RoutePath& path, Vec aim, const Segment& s, int j) const {
        return cf->block && path.armedSwing && path.hit == j &&
            cone(point(path.body[j + 1]), s.from, aim, cf->slash_reach, cf->block_cos / 1000., 1, now + j);
    }

    void predictClashes(Exchange& ex, const RoutePath& path, Vec aim) const {
        if (!cf->clash ||
            std::none_of(ex.projectile.begin(), ex.projectile.end(), [](const Segment& s) { return s.active; })) return;
        for (const auto& h : hazards) {
            if (!h.observed) continue;
            for (int j = 0; j + 1 < Horizon; ++j) {
                const Segment& s = h.ray[j];
                if (!s.active) continue;
                if (blocksAt(path, aim, s, j)) break;
                if (clashPoint(ex.projectile[j], s) < 0) continue;
                // The referee resolves every clash on this step before grounding darts.
                ex.clashBefore[h.owner][j + 1] += h.weight;
                ex.hasClash = true;
                break;
            }
        }
        if (!ex.hasClash) return;
        for (auto& before : ex.clashBefore) {
            for (int j = 1; j < Horizon; ++j) before[j] = clip(before[j] + before[j - 1]);
        }
    }

    Exchange exchange(const RoutePath& path, const Route& route, Vec aim) const {
        Exchange out;
        if (!CoupledCombat) return out;
        Dart d = obs->my_dart();
        int shot = -1;
        if (route.kind == A_THROW) shot = int(cf->throw_pre);
        else if (obs->me().st == S_THROW) shot = int(obs->me().timer);
        if (route.kind == A_RECALL) {
            d.st = D_RISE; d.left = obs->me().st == S_RECALL ? obs->me().timer : cf->recall_pre;
        } else if (d.st == D_RISE && route.move.sq() > 0) {
            d.st = D_GROUND;
        }
        for (int j = 0; j < Horizon; ++j) {
            if (j == shot) {
                const Vec a = route.kind == A_THROW ? route.aim : heading(obs->me());
                d = release(point(path.body[j]), a);
            }
            if (d.st == D_RISE && --d.left <= 0) { d.st = D_CALL; d.age = 0; }
            if (dart_flying(d.st)) {
                out.projectile[j] = fly(d, point(path.body[j]), point(path.body[j + 1]), now + j);
                if (d.st == D_HAND) out.catchAt = j + 1;
            }
        }
        predictClashes(out, path, aim);
        std::array<double, Horizon> ownFlight;
        ownFlight.fill(1.);
        if (out.hasClash) for (const auto& before : out.clashBefore) {
            for (int j = 1; j < Horizon; ++j) ownFlight[j] *= 1. - before[j];
        }
        for (int k = 0; k < obs->n; ++k) {
            const auto& target = enemy[k];
            if (!target.present || (point(target.path[0][0]) - here).sq() > 10300. * 10300.) continue;
            std::array<double, Models> chances{};
            for (int j = 0; j < Horizon; ++j) {
                const Segment& own = out.projectile[j];
                double stopMass = out.incapacitated[k][j];
                if (!own.active && path.hit != j) { out.incapacitated[k][j + 1] = stopMass; continue; }
                for (int m = 0; m < Models; ++m) {
                    if (chances[m] >= 0.99) continue;
                    const Player& before = target.path[m][j];
                    const Vec q = point(target.path[m][j + 1]);
                    double chance = chances[m];
                    if (own.active) {
                        const double miss2 = segmentDistance2(own.from - point(before), own.to - q);
                        if (miss2 < (hitRadius + 120) * (hitRadius + 120)) {
                            const double margin = (hitRadius + 120 - std::sqrt(miss2)) / 380.;
                            const bool reacts = j >= cf->delay + 1 && before.st == S_FREE;
                            chance = std::max(chance, ownFlight[j] * (reacts ? 0.46 : 0.94) * clip(margin));
                        }
                    }
                    if (path.hit == j && (path.armedSwing || (before.st != S_STUN &&
                        !(before.st == S_FREE && before.timer > 0)))) {
                        const double reach = path.armedSwing ? cf->slash_reach : cf->kick_reach;
                        const double cosine = double(path.armedSwing ? cf->slash_cos : cf->kick_cos) / 1000;
                        const Vec pos = point(path.body[j + 1]);
                        if (cone(pos, q, aim, reach, cosine, cf->body_r, now + j))
                            chance = std::max(chance, (path.armedSwing ? 0.96 : 0.65) * clip((reach + 150 - (q - pos).len()) / 310));
                    }
                    stopMass += target.weight[m] * (chance - chances[m]) * target.reach;
                    chances[m] = chance;
                }
                out.incapacitated[k][j + 1] = stopMass;
            }
        }
        return out;
    }

    double risk(const RoutePath& path, Vec aim, bool committed, bool allowFutureBlock, const Exchange& expected) const {
        double cost = 0, disarm = 0;
        std::array<double, MAXP> seenRisk{}, blindRisk{}, blocks{};
        for (const auto& h : hazards) {
            double hit = 0;
            double hitBeforeClash = 0;
            double clashProbability = -1;
            for (int j = 0; j < Horizon; ++j) {
                const Segment& s = h.ray[j];
                if (!s.active) continue;
                if (blocksAt(path, aim, s, j)) {
                    if (h.observed && clashProbability < 0) {
                        const double separation = (s.from - point(enemy[h.owner].path[0][j])).len();
                        blocks[h.owner] += h.weight * (0.63 + 0.37 * clip(separation / 3400)) / (1 + 0.19 * j);
                    }
                    break;
                }
                const Segment& own = expected.projectile[j];
                Vec end = s.to;
                bool clash = false;
                if (CoupledCombat && cf->clash && clashProbability < 0) {
                    const double at = clashPoint(own, s);
                    if (at >= 0) {
                        clash = true;
                        end = s.from + (end - s.from) * at;
                        if (expected.hasClash && h.observed) {
                            clashProbability = 1.;
                            // The same owner's paths are alternatives to this incoming path.
                            for (int k = 0; k < obs->n; ++k) if (k != h.owner)
                                clashProbability *= 1. - expected.clashBefore[k][j];
                        }
                    }
                }
                const Vec from = s.from - point(path.body[j]);
                const Vec after = point(path.body[j + 1]);
                const double margin = hitRadius + (h.observed ? 180 : 250);
                double weight = 1.;
                if (h.observed) {
                    if (j > 5) weight *= committed ? 0.70 : 0.43;
                    if (allowFutureBlock && j >= 2 && armed) weight *= 0.83;
                } else {
                    weight *= j < 4 ? 0.95 : j < 8 ? 0.64 : 0.34;
                }
                if (CoupledCombat) {
                    const int when = live == 1 ? j : (h.observed ? 0 : std::max(0, h.launch));
                    weight *= 1 - 0.83 * expected.incapacitated[h.owner][when];
                }
                const auto chance = [&](Vec to) {
                    double dd = segmentDistance2(from, to - after);
                    if (dd >= margin * margin) return 0.;
                    return weight * (dd <= hitRadius * hitRadius ? 1. : (margin - std::sqrt(dd)) / (margin - hitRadius) * 0.45);
                };
                if (clash && clashProbability >= 0) hitBeforeClash = std::max(hit, chance(end));
                hit = std::max(hit, chance(clashProbability >= 0 ? s.to : end));
                if (clash && h.observed && (clashProbability < 0 || clashProbability >= 1. - 1e-9)) {
                    if (clashProbability >= 0) hit = hitBeforeClash;
                    break;
                }
            }
            if (clashProbability >= 0) hit += clashProbability * (hitBeforeClash - hit);
            if (h.observed) seenRisk[h.owner] += h.weight * hit;
            else blindRisk[h.owner] += h.weight * hit;
        }
        for (int k = 0; k < obs->n; ++k) {
            cost += 23.0 * std::min(1., seenRisk[k]) + BlindPenalty * (committed ? 1.25 : 1.) * std::min(0.75, blindRisk[k]);
            disarm += std::min(1., blocks[k]) * (enemy[k].present ? 5.5 : 1.3);
        }
        return disarm - cost;
    }

    double contactRisk(const RoutePath& path, const Route& route) const {
        double loss = 0;
        for (int k = 0; k < obs->n; ++k) {
            const auto& e = enemy[k];
            if (!e.present || (point(e.path[0][0]) - here).sq() > 6800. * 6800.) continue;
            const Player& old = obs->p[k];
            double worst = 0;
            for (int j = 0; j <= 7; ++j) {
                Vec p = point(path.body[j + 1]);
                const Player& en = e.path[0][j + 1];
                double d = (point(en) - p).len();
                if (d > 3250 || wall(p, point(en), now + j)) continue;
                bool trade = path.armedSwing && path.hit >= 0 && path.hit <= j &&
                    cone(point(path.body[path.hit + 1]), point(e.path[0][path.hit + 1]), route.aim.unit(),
                         cf->slash_reach, cf->slash_cos / 1000., cf->body_r, now + path.hit);
                if (old.st == S_SLASH && old.timer == now - imax(0, now - cf->delay) + j + 1 &&
                    cone(point(en), p, heading(old).unit(), cf->slash_reach, cf->slash_cos / 1000., cf->body_r, now + j))
                    worst = std::max(worst, trade ? 5.5 : 12.);
                if (en.st != S_FREE && en.st != S_DASH && en.st != S_RECALL) continue;
                if (e.armed || (WeaponClock && e.readyAt <= j + 1)) {
                    double canSwing = clip((1980 - d) / 700);
                    double canDash = en.cd <= 2 ? clip((3020 - d) / 1400) : 0;
                    double pressure = trade ? 0.86 : route.kind == A_THROW ? 3.7 : armed ? 2.25 : 4.1;
                    worst = std::max(worst, pressure * std::max(canSwing, 0.46 * canDash) / (1 + 0.23 * j));
                } else if (d < 1680 && (path.body[j].st == S_FREE || path.body[j].st == S_DASH || path.body[j].st == S_RECALL)) {
                    Vec kickDirection = (p - point(en)).unit(2800);
                    if (pit(p + kickDirection, now + j + cf->kick_stun))
                        worst = std::max(worst, (trade ? 0.4 : 2.2) * clip((1750 - d) / 430));
                }
            }
            loss += worst * e.reach;
        }
        return loss;
    }

    double returnValue(const RoutePath& path, const Route& r) const {
        Dart d = obs->my_dart();
        if (!(dart_flying(d.st) || d.st == D_RISE || r.kind == A_RECALL)) return 0;
        if (r.kind == A_RECALL) { d.st = D_RISE; d.left = cf->recall_pre; }
        if (d.st == D_RISE && r.move.sq() > 0) return 0;
        double value = 0;
        std::array<std::array<bool, Models>, MAXP> credited{};
        for (int j = 0; j < Horizon; ++j) {
            if (d.st == D_RISE && --d.left <= 0) { d.st = D_CALL; d.age = 0; }
            Segment line = fly(d, point(path.body[j]), point(path.body[j + 1]), now + j);
            if (line.active) for (int k = 0; k < obs->n; ++k) {
                const auto& e = enemy[k];
                if (!e.present || (point(e.path[0][j]) - line.from).sq() > 4200. * 4200.) continue;
                for (int m : {0, 1, 3, 4}) {
                    if (credited[k][m]) continue;
                    double miss = segmentDistance2(line.from - point(e.path[m][j]), line.to - point(e.path[m][j + 1]));
                    if (miss <= hitRadius * hitRadius) {
                        credited[k][m] = true;
                        value += 4.8 * e.weight[m] * e.reach / (1 + 0.10 * j);
                    }
                }
            }
            if (d.st == D_HAND) return value + 1.4 / (1 + 0.045 * j);
            if (d.st == D_GROUND) return value - 0.55;
        }
        if (r.kind == A_RECALL || d.st == D_RISE) {
            double danger = live > 1 ? 0.95 : 0.8;
            if (focus >= 0 && wall(here, point(enemy[focus].path[0][0]), now)) danger *= 0.53;
            value += std::clamp((closest - 2300) / 1300, -2., 2.5) - danger;
        }
        return value;
    }

    double evaluate(const Route& r, double attack = 0) const {
        RoutePath p = simulate(r);
        if (p.fell) return -1000;
        bool committed = r.kind != A_NONE || obs->me().st == S_DASH || obs->me().st == S_RECALL;
        Vec aim = p.hit >= 0 && r.kind != A_MELEE && r.swingAt < 0 ? heading(obs->me()).unit() : r.aim.unit();
        const Exchange expected = exchange(p, r, aim);
        const bool hold = (armed && r.kind != A_THROW) || p.picked;
        const auto holdingAt = [&](int j) { return hold || (CatchAwareness && expected.catchAt <= j - 1); };
        double value = 0.35 * position(point(p.body[4]), 4, holdingAt(4)) +
                       0.37 * position(point(p.body[8]), 8, holdingAt(8)) +
                       0.28 * position(point(p.body[Horizon]), Horizon, holdingAt(Horizon));
        value += risk(p, aim, committed, r.kind == A_NONE && r.swingAt < 0, expected) *
            (live > 1 ? 1.30 : 1.);
        value += returnValue(p, r);
        value -= contactRisk(p, r);
        if (p.hit >= 0) value += melee(p, aim);
        if (r.kind == A_THROW) value += attack - (armed && live <= 1 ? 1.25 : 0.65);
        if (r.kind == A_MELEE || r.swingAt >= 0) value -= p.armedSwing ? 1.14 : 0.75;
        if (r.kind == A_DASH) {
            value -= 0.38;
            if (armedPressure) {
                const Vec landing = point(p.body[imin(Horizon, cf->dash_pre + cf->dash_t)]);
                for (int k = 0; k < obs->n; ++k) if (enemy[k].present && enemy[k].armed) {
                    Vec threat = point(enemy[k].path[0][7]);
                    if ((landing - threat).sq() < 6200. * 6200. && !wall(landing, threat, now + 7)) {
                        value -= DashPenalty;
                        break;
                    }
                }
            }
        }
        if (LockPenalty && (r.kind == A_DASH || r.kind == A_RECALL) && closest < 8000) {
            const int seen = int(cf->delay) + 1;
            const int shot = seen + int(cf->throw_pre);
            const Vec lock = point(p.body[std::min(Horizon, shot + 2)]);
            for (int k = 0; k < obs->n; ++k) {
                const auto& e = enemy[k];
                if (!e.present || !e.visible || (e.readyAt > seen && !e.armed)) continue;
                const Vec origin = point(e.path[0][std::min(shot, Forecast)]);
                const double d2 = (origin - lock).sq();
                if (d2 > 6800. * 6800. || wall(origin, lock, now + shot)) continue;
                const bool unable = p.body[std::min(Horizon, shot + 2)].st != S_FREE;
                const double hold = r.kind == A_RECALL ? 2.2 : unable ? 1.85 : 0.70;
                value -= hold * clip((6800 - std::sqrt(d2)) / 3600);
            }
        }
        if (p.picked) value += 2.0;
        if (r.kind == A_NONE) value += 0.105 * r.move.dot(carry) / 1000000;
        return value;
    }

    Vec swingAim(Vec source, int tick) const {
        double score = -1;
        Vec aim{1000, 0};
        for (int k = 0; k < obs->n; ++k) {
            const auto& e = enemy[k];
            if (!e.present) continue;
            Vec target{};
            for (int m = 0; m < Models; ++m) target = target + point(e.path[m][tick]) * e.weight[m];
            double d = (target - source).len();
            double value = e.reach * (3800 - d);
            if (value > score) { score = value; aim = (target - source).unit(1000); }
        }
        return aim;
    }

    double shotScore(Vec origin, Vec aim, int k) const {
        Dart d = release(origin, aim);
        const auto& e = enemy[k];
        std::array<double, Models> scores{};
        int ready = cf->throw_pre;
        const int canReact = int(cf->delay) + 1;
        if (cf->throw_pre <= 1) --ready;
        for (int j = ready; j < 17; ++j) {
            Segment s = fly(d, origin, origin, now + j);
            for (int m = 0; m < Models; ++m) {
                if (scores[m] >= 0.99) continue;
                const Player& before = e.path[m][j];
                const Player& q = e.path[m][j + 1];
                if ((s.from - point(before)).sq() > 3200. * 3200.) continue;
                double miss = segmentDistance2(s.from - point(before), s.to - point(q));
                if (miss > hitRadius * hitRadius) continue;
                double chance = 1.0;
                if (j >= canReact && (before.st == S_FREE || before.st == S_DASH)) {
                    chance = e.armed ? 0.20 : 0.44;
                    if (j == canReact) chance += 0.27;
                    if (std::sqrt(miss) < 300 && j == canReact) chance += 0.10;
                }
                if (m == 0 && (q.st == S_FREE || q.st == S_THROWN))
                    chance *= 1 - 0.27 * clip(std::sqrt(miss) / hitRadius);
                scores[m] = std::max(chance, scores[m]);
            }
            if (d.st != D_OUT) break;
        }
        double total = 0;
        for (int m = 0; m < Models; ++m) total += e.weight[m] * scores[m];
        if (d.st == D_GROUND && total < 0.55) total *= 0.79;
        return total * e.reach;
    }

    Shot bestShot(Vec movement) const {
        Shot best{movement, {}, 0};
        Player releaseBody = obs->me();
        begin(releaseBody, A_THROW, {1000, 0}, true, movement);
        for (int j = 0; j < cf->throw_pre; ++j) if (!advance(releaseBody, movement, now + j)) return best;
        const Vec origin = point(releaseBody);
        for (int k = 0; k < obs->n; ++k) {
            const auto& e = enemy[k];
            if (!e.present || !e.visible || (point(e.path[0][0]) - origin).sq() > 7300. * 7300.) continue;
            Vec lastAim{};
            for (int m = 0; m < Models; ++m) {
                double travelled = 0;
                int j = int(cf->throw_pre);
                for (; j < 15; ++j) {
                    travelled += std::max(cf->dart_vmin, cf->dart_v - (j - cf->throw_pre) * cf->dart_dec);
                    if ((point(e.path[m][j + 1]) - origin).len() <= travelled + 0.2 * hitRadius) break;
                }
                Vec aim = (point(e.path[m][j + 1]) - origin).unit(1000);
                if ((aim - lastAim).sq() < 32 * 32) continue;
                lastAim = aim;
                double probability = shotScore(origin, aim, k);
                const double dist = (point(e.path[0][0]) - origin).len();
                const bool hasWeapon = e.armed || (WeaponClock && e.readyAt <= j + 1);
                double reserve = hasWeapon ? (live > 1 ? 0.33 : 0.45) : 0.26;
                int state = e.path[0][std::max(0, std::min(j, 8) - 3)].st;
                bool vulnerable = state == S_DASH || state == S_STUN || state == S_RECALL || state == S_SLASHED || state == S_KICKED;
                if (live > 1 && !vulnerable) reserve += 0.10;
                if (vulnerable && dist < 5900) reserve -= 0.062;
                if (obs->me().expose == 0 && dist > cf->see_r &&
                    cell_of_point(*cf, field(now).map, obs->me().x, obs->me().y, -1) == C_GRASS) reserve -= 0.055;
                if (live == 1 && !hasWeapon && !vulnerable && dist > 3600) reserve += 0.070;
                double value = (probability - reserve) * 11.4;
                if (dist < 2200 && e.armed) value -= 0.65;
                if (value > best.value) best = Shot{movement, aim, value};
            }
        }
        return best;
    }

    Action decide(const Obs& o) {
        const auto started = std::chrono::steady_clock::now();
        obs = &o; now = int(o.t); here = point(o.me());
        if (!o.me().alive) return {};
        self[now % History] = o.me();
        own[now % History] = o.my_dart();
        armed = o.my_dart().st == D_HAND;
        const int ref = int(imax(0, now - cf->delay));
        track();
        inferContact();
        inferImpact();
        const bool moving = o.me().st == S_FREE || o.me().st == S_THROW ||
            o.me().st == S_THROWN || o.me().st == S_RECALL;
        const bool ready = o.me().st == S_FREE || o.me().st == S_RECALL;
        if (!moving && !(cf->dash_slash && dashMoves(o.me()))) {
            carry = {};
            moves[now % History] = {};
            return {};
        }
        hazards.clear();
        for (int k = 0; k < o.n; ++k) if (k != seat) {
            enemy[k].readyAt = enemy[k].armed ? 0 : Horizon + 8;
            knownThreats(k, ref);
            auto& e = enemy[k];
            if (e.visible && o.d[k].st == D_GROUND) {
                const auto pickup = std::find_if(e.groundPickup.begin(), e.groundPickup.end(), [](double p) { return p >= 0.55; });
                if (pickup != e.groundPickup.end()) {
                    e.readyAt = int(pickup - e.groundPickup.begin()) - MAX_DELAY;
                    armedPressure |= e.readyAt <= 2 && (point(e.path[0][0]) - here).sq() < 7600. * 7600.;
                }
            }
            if (ImpactInference && e.clashTick > ref) {
                double dist = (point(e.path[0][0]) - e.impact).len();
                int pickup = int(std::max(0., dist - cf->pick_r - 220) / cf->run_v);
                if (pickup < 8) e.readyAt = std::min(e.readyAt, pickup);
            }
            if (ImpactInference && e.blockTick > ref)
                e.readyAt = std::max(e.readyAt, int(cf->slash_post - (now - e.blockTick)));
            blindThreats(k, ref);
            crossingThreats(k);
        }
        navigation();
        Route best{};
        double bestValue = -Infinity;
        bool budgetSpent = false;
        int tried = 0;
        auto consider = [&](Route r, double attack = 0.) {
            // One decision may be preempted on a busy host. Return the best completed
            // candidate once the wall-time cap leaves insufficient CPU headroom.
            if (budgetSpent) return -Infinity;
            if (++tried % 12 == 0 && std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(4)) {
                budgetSpent = true;
                return -Infinity;
            }
            double value = evaluate(r, attack);
            if (value > bestValue) { best = r; bestValue = value; }
            return value;
        };
        consider({});
        Vec toGoal = (goal - here).unit(1000);
        if (moving) {
            std::array<Route, 3> walk{};
            std::array<double, 3> walkValue{};
            walkValue.fill(-Infinity);
            for (Vec d : dirs) {
                double v = consider({d});
                if (bestValue < -0.7 || armedPressure) consider({d, {}, {}, A_NONE, 5});
                if (CurvedRoutes && (armedPressure || !hazards.empty()) && closest < 6500) {
                    for (int i = 0; i < int(walk.size()); ++i) if (v > walkValue[i]) {
                        for (int j = int(walk.size()) - 1; j > i; --j) {
                            walk[j] = walk[j - 1]; walkValue[j] = walkValue[j - 1];
                        }
                        walk[i] = {d}; walkValue[i] = v;
                        break;
                    }
                }
            }
            consider({carry});
            consider({toGoal});
            if (hasGuide) { consider({routeGuide}); consider({routeGuide * 0.47}); }
            const Route bestWalk = best;
            if (!hazards.empty() || closest < 4000) {
                for (int i = 0; i < Angles; i += 3)
                    consider({dirs[i], bestWalk.move, {}, A_NONE, 4});
                if (hasGuide)
                    consider({bestWalk.move, routeGuide, {}, A_NONE, 4});
            }
            if (CurvedRoutes && walkValue[0] > -900) {
                const Vec radial = focus >= 0 ? (point(enemy[focus].path[0][0]) - here).unit(1000) : toGoal;
                for (int i = 0; i < int(walk.size()); ++i) if (walkValue[i] > -900) {
                    for (Vec second : {radial.left(), radial.left() * -1, bestWalk.move, Vec{}})
                        consider({walk[i].move, second, {}, A_NONE, 3 + i});
                }
            }
        }
        if (ready && o.me().cd == 0) {
            const Vec follow = hasGuide ? routeGuide : toGoal;
            for (int i = 0; i < Angles; i += 2) {
                Vec dir = dirs[i];
                consider({follow, {}, dir, A_DASH});
                if (cf->dash_slash && closest < 5100) {
                    for (int dashSteps : {1, 3}) {
                        int hit = int(cf->dash_pre) + dashSteps + int(armed ? cf->slash_pre : cf->kick_pre);
                        Vec landing = here + dir * (cf->dash_v * (dashSteps + 1) / 1000.);
                        Vec aim = swingAim(landing, hit);
                        Route r{{}, {}, aim, A_DASH, Horizon, int(cf->dash_pre) + dashSteps};
                        // A combined route starts the dash along dir and uses its swing aim at cancellation.
                        r.follow = dir;
                        consider(r);
                    }
                }
            }
            if (hasGuide) consider({routeGuide, {}, routeGuide, A_DASH});
        }
        if (ready || (cf->dash_slash && dashMoves(o.me()))) {
            const int hit = int(armed ? cf->slash_pre : cf->kick_pre) - 1;
            Route r{{}, {}, {1000, 0}, A_MELEE};
            if (closest < 3650 || (armed && !hazards.empty())) {
                auto p = simulate(r);
                if (!p.fell) {
                    const Vec balance = point(p.body[hit + 1]);
                    r.aim = swingAim(balance, hit + 1);
                    if (closest < 3650) {
                        consider(r);
                        for (int k = 0; k < o.n; ++k) if (enemy[k].present) {
                            for (int m : {0, 3, 4}) {
                                Vec delta = point(enemy[k].path[m][hit + 1]) - balance;
                                if (delta.sq() < 2500. * 2500.) {
                                    r.aim = delta.unit(1000);
                                    consider(r);
                                }
                            }
                        }
                    }
                    if (armed && cf->block) for (const auto& h : hazards) {
                        const auto& s = h.ray[hit];
                        if (!h.observed || !s.active || (s.from - balance).sq() > cf->slash_reach * cf->slash_reach) continue;
                        r.aim = (s.from - balance).unit(1000);
                        consider(r);
                    }
                }
            }
        }
        if (ApproachParry && ready && armed && cf->block) {
            for (int j = 2; j <= 5; ++j) {
                const int hit = j + int(cf->slash_pre) - 1;
                for (const auto& h : hazards) {
                    const auto& s = h.ray[hit];
                    if (!h.observed || !s.active || (s.from - here).sq() > 3150. * 3150.) continue;
                    for (Vec mv : {(s.from - here).unit(1000), best.move}) {
                        Player p = o.me();
                        for (int step = 0; step < j; ++step) advance(p, mv, now + step);
                        if ((s.from - point(p)).sq() > cf->slash_reach * cf->slash_reach) continue;
                        Route r{mv, {}, (s.from - point(p)).unit(1000), A_NONE, Horizon, j};
                        consider(r);
                    }
                }
            }
        }
        if (!budgetSpent && ready && armed && closest < 7250) {
            const Vec movement = best.kind == A_NONE ? best.move : carry;
            for (Vec d : {movement, Vec{}, toGoal.left(), toGoal.left() * -1}) {
                if (budgetSpent) break;
                Shot shot = bestShot(d);
                if (shot.value > 0) consider({shot.move, {}, shot.aim, A_THROW}, shot.value);
            }
        }
        if (ready && cf->recall && o.my_dart().st == D_GROUND) consider({{}, {}, {}, A_RECALL});
        Vec aim = best.kind == A_DASH && best.swingAt >= 0 ? best.follow : best.aim;
        Action a = order(best.move, best.kind, aim);
        if (bestValue < -900) a = {};
        carry = {double(a.mx), double(a.my)};
        moves[now % History] = a;
        return a;
    }
};

} // namespace

DART_MAIN(Bot)
