#pragma once

#include <QColor>
#include <QCursor>
#include <QImage>
#include <QPair>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

#include "AiClient.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;

// 标注工具。None 表示只选区域、还没选画笔或图形。
// Rectangle / Ellipse / Triangle / Line 在工具条上合并在"形状"一个按钮下
enum class MarkTool
{
    None,
    Pen,
    Rectangle,
    Ellipse,
    Triangle,
    Line,
    Text,
    Eraser,
};

// 一笔标注。画笔用 points 记录轨迹，矩形/圆/三角/直线用 start 与 end 定义，
// 文本用 start 记录左上角位置、text 存内容。
// erasedRanges 是形状被橡皮擦掉的区间：参数沿边框归一化到 0~1（闭合路径绕一圈），
// 渲染时跳过这些区间——形状本体不变，所以擦剩的圆弧仍然是真圆弧
struct MarkShape
{
    MarkTool tool = MarkTool::None;
    QPoint start;
    QPoint end;
    QVector<QPoint> points;
    QString text;
    QVector<QPair<qreal, qreal>> erasedRanges;
    QColor color = QColor(230, 60, 60);
    int lineWidth = 3;    // 画笔笔迹和形状边线的粗细
    int fontSize = 24;    // 文字标注的字号，只有 Text 用
};

// 截图覆盖层：铺满一块屏幕，外面压暗、选区还原成原样，
// 选好区域后在选区旁弹出横向工具条，可以复制、保存、置顶或继续标注。
//
// 多屏时每块屏幕各建一层，而不是用一个大窗口横跨所有屏幕：
// 一块屏幕上只有一个分辨率和缩放比例，混合 DPI 的多屏才不会出现
// 坐标对不上、截图糊掉的问题
class ScreenshotOverlay : public QWidget
{
    Q_OBJECT

public:
    // screenImage 是预先抓好的这块屏幕的图像，coveredGeometry 是它对应的屏幕区域（全局坐标）
    ScreenshotOverlay(const QImage &screenImage, const QRect &coveredGeometry, qreal imagePixelRatio);

signals:
    // 在这块屏幕上开始拖选新区域。其它屏幕的遮罩收到后应该收起来
    void selectionStarted();

    // 这层遮罩已经结束（复制、保存、置顶、取消都算），只发一次
    void captureClosed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildToolBar();              // 创建选区下方那条横向菜单，只会建一次
    void placeToolBar();              // 把菜单放到选区的下方，空间不够则放上方
    void applyTool(MarkTool tool);    // 切换标注工具，同时更新按钮高亮
    void undoLastMark();             // 撤销最后一笔标注
    void chooseMarkColor();          // 弹出取色框，修改标注颜色
    void updateColorButtonStyle();   // 让工具条上的色块显示当前颜色
    void setMarkWidth(int lineWidth);   // 修改画笔粗细，同步按钮图标和面板上的数字
    void setEraserRadius(int radius);   // 修改橡皮擦大小
    void syncWidthPanel();           // 让面板滑杆、数字与当前工具的值保持一致
    void updateWidthPanelForTool();  // 按当前工具切换面板的标题和滑杆范围
    void updateWidthButtonIcon();    // 按当前粗细重画粗细按钮上的圆点
    void showWidthPanel();           // 在工具条外侧弹出粗细滑杆面板
    void showShapePanel();           // 在形状按钮旁弹出二级形状菜单
    void showTextPanel();            // 在文字按钮旁弹出文字面板（字号 + 颜色）
    QWidget *buildTextPanel();       // 创建文字面板，只建一次
    void setTextSize(int size);      // 修改文字字号
    void setTextColor(const QColor &color);  // 修改文字颜色
    void updateTextPanelState();     // 让面板里的滑杆和色块反映当前设置
    void setToolCursor();            // 按当前工具换鼠标光标（橡皮擦靠提示圈指示）
    void startTextEntry(const QPoint &position);  // 在点击位置弹出文本输入框
    void commitTextEntry();          // 把输入框里的文字提交成一条文本标注
    void discardTextEntry();         // 关掉输入框但不留下标注（Esc 用）
    void updateTextEditorGeometry(); // 按当前字号重算输入框的字体、大小和位置
    void clampTextEditorPosition();  // 把输入框夹回选区内，拖动和改字号后都要走一遍
    int edgeAt(const QPoint &position) const;   // 判断鼠标落在选区哪条边上，返回边的位组合
    void resizeSelectionTo(const QPoint &position);  // 按当前抓取的边把选区拉到鼠标位置
    void eraseAt(const QPoint &position);   // 橡皮擦：擦掉经过位置命中的标注
    QImage renderResult() const;      // 把选中区域和标注合成为最终图片
    QImage cropSelection() const;     // 只裁出选中区域，不带标注
    void recognizeText();             // 把选区交给模型提取文字
    void translateText();             // 把选区交给模型识别并翻译
    void sendToAi(AiClient::Task task);   // 上面两个动作共用：裁图、检查配置、打开结果窗口
    void copyResult();
    void saveResult();
    void pinResult();
    void cancelCapture();             // 放弃本次截图，关闭覆盖层

    QImage screenshot;
    QImage maskedImage;   // 预先叠好黑色蒙版的底图，绘制时直接贴，省去每次重绘的混合运算
    QRect screenGeometry; // 这块遮罩盖住的那块屏幕（全局坐标）
    qreal pixelRatio = 1.0;

    // 结束通知只发一次：close() 可能被多处调用（按钮、Esc、其它屏幕的遮罩）
    bool closeNotified = false;

    QRect selection;
    QPoint dragStart;
    bool selecting = false;

    // 选好区域后按住选区内部可以整体拖动，而不是重新框选
    bool movingSelection = false;
    QPoint moveOffset;

    // 按住选区边缘或角可以拉伸调整大小。
    // resizeEdges 用位组合记录抓住了哪几条边，resizeOriginal 是按下那一刻的矩形，
    // 拉伸始终基于它计算，避免逐帧累加带来的抖动
    bool resizingSelection = false;
    int resizeEdges = 0;
    QRect resizeOriginal;

    MarkTool currentTool = MarkTool::None;
    QVector<MarkShape> shapes;
    MarkShape drawingShape;
    bool drawing = false;
    bool erasing = false;   // 橡皮擦按下并拖动期间为 true

    // 新画的标注用这个颜色，工具条上的色块显示的就是它
    QColor markColor = QColor(230, 60, 60);

    // 文字标注单独一套样式，不和画笔共用：
    // 画笔常配粗红线条，而文字往往要细一号、颜色也可能不一样，
    // 两者共用一套设置时改画笔就会顺手改掉已经写好的字。
    // 这两个值只作用于"正在写的这条"和"接下来写的"，已经提交的标注各自记着自己的样式
    QColor textColor = QColor(230, 60, 60);
    int textSize = 24;

    // 新画的标注用这个粗细（1~20），工具条上的粗细按钮显示的就是它。
    // 文本标注的字号跟它联动；调整时已画形状的边线粗细也实时跟随，
    // 画笔笔迹保持画下时的粗细不变（长笔迹整体变粗会很怪）
    int markWidth = 3;

    // 橡皮擦的擦除半径（4~48），由粗细面板在橡皮擦工具下调节
    int eraserRadius = 10;
    // 橡皮擦模式下的鼠标位置，用于在画面上画出擦除范围提示圈
    QPoint eraserPreviewPos;
    bool eraserPreviewVisible = false;

    // 文本工具的内联输入框。只在输入期间存在，提交或放弃后销毁。
    // 整个容器都能按住拖动（和拖动截图选区一个手感）——
    // 文字没落定之前位置还能调，一旦提交就画进图里，不能再动
    QWidget *textEditorBox = nullptr;
    QLineEdit *textEditor = nullptr;
    QPoint textEditPos;      // 编辑区（真正写字的地方）在覆盖层里的位置
    QPoint textEditAnchor;   // 容器左上角的位置，改字号、重新夹取都以它为准

    // 拖动输入框期间为 true，记录鼠标相对容器左上角的偏移
    bool draggingTextEditor = false;
    QPoint textEditorDragOffset;
    QPoint textEditorPressPosition;   // 在文字区按下时的全局坐标，用来判断有没有真的拖动

    QWidget *toolBar = nullptr;
    QPushButton *penButton = nullptr;
    QPushButton *shapeButton = nullptr;       // 形状按钮（二级菜单里选具体形状）
    QPushButton *textButton = nullptr;
    QPushButton *eraserButton = nullptr;
    QPushButton *colorButton = nullptr;
    QPushButton *widthButton = nullptr;

    // 形状二级菜单：点形状按钮弹出，横排四个形状按钮
    QWidget *shapePanel = nullptr;
    QVector<QPushButton *> shapeToolButtons;
    QVector<MarkTool> shapeToolKinds;

    // 粗细面板：点工具条上的粗细按钮才弹出来，里面是一条滑杆。
    // 画笔工具下调粗细（1~20），橡皮擦工具下调整个擦除范围（4~48）
    QWidget *widthPanel = nullptr;
    QSlider *widthSlider = nullptr;
    QLabel *widthTitleLabel = nullptr;
    QLabel *widthValueLabel = nullptr;

    // 文字面板：点文字按钮弹出，上面是字号滑杆，下面是一排预设颜色。
    // 选中色块用强调色描边标出来，找不到合适的颜色时点"更多"自己调
    QWidget *textPanel = nullptr;
    QSlider *textSizeSlider = nullptr;
    QLabel *textSizeValueLabel = nullptr;
    QVector<QPushButton *> textColorButtons;
    QVector<QColor> textColorPresets;
};
