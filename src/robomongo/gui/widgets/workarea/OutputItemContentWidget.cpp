#include "robomongo/gui/widgets/workarea/OutputItemContentWidget.h"

#include <QVBoxLayout>
#include <QFileDialog>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <Qsci/qscilexerjavascript.h>

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/EventBus.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/core/utils/QtUtils.h"
#include "robomongo/core/domain/MongoShell.h"
#include "robomongo/core/domain/MongoServer.h"
#include "robomongo/core/mongodb/MongoWorker.h"
#include "robomongo/core/domain/MongoAggregateInfo.h"

#include "mongo/bson/json.h"

#include "robomongo/gui/widgets/workarea/OutputWidget.h"
#include "robomongo/gui/widgets/workarea/OutputItemHeaderWidget.h"
#include "robomongo/gui/widgets/workarea/JsonPrepareThread.h"
#include "robomongo/gui/widgets/workarea/BsonTreeView.h"
#include "robomongo/gui/widgets/workarea/BsonTreeModel.h"
#include "robomongo/gui/widgets/workarea/BsonTableView.h"
#include "robomongo/gui/widgets/workarea/BsonTableModel.h"
#include "robomongo/gui/editors/PlainJavaScriptEditor.h"
#include "robomongo/gui/widgets/workarea/CollectionStatsTreeWidget.h"
#include "robomongo/gui/GuiRegistry.h"
#include "robomongo/gui/editors/JSLexer.h"
#include "robomongo/gui/editors/FindFrame.h"

namespace Robomongo
{
    OutputItemContentWidget::OutputItemContentWidget(ViewMode viewMode, MongoShell *shell, 
                                                     const QString &text, double secs, bool multipleResults, 
                                                     bool tabbedResults, bool firstItem, bool lastItem,
                                                     AggrInfo aggrInfo, const QString &collectionName,
                                                     qint64 totalCount, QWidget *parent) :
        BaseClass(parent),
        _textView(NULL),
        _bsonTreeview(NULL),
        _thread(NULL),
        _bsonTable(NULL),
        _isTextModeSupported(true),
        _isTreeModeSupported(false),
        _isTableModeSupported(false),
        _isCustomModeSupported(false),
        _isTextModeInitialized(false),
        _isTreeModeInitialized(false),
        _isCustomModeInitialized(false),
        _isTableModeInitialized(false),
        _isFirstPartRendered(false),
        _text(text),
        _shell(shell),
        _outputWidget(dynamic_cast<OutputWidget*>(parentWidget())),
        _initialSkip(0),
        _initialLimit(0),
        _mod(NULL),
        _viewMode(viewMode),
        _aggrInfo(aggrInfo),
        _collectionName(collectionName),
        _totalCount(totalCount)
    {
        setup(secs, multipleResults, tabbedResults, firstItem, lastItem);
    }

    OutputItemContentWidget::OutputItemContentWidget(ViewMode viewMode, MongoShell *shell, 
                                                     const QString &type, 
                                                     const std::vector<MongoDocumentPtr> &documents, 
                                                     const MongoQueryInfo &queryInfo, double secs, 
                                                     bool multipleResults, bool tabbedResults,
                                                     bool firstItem, bool lastItem, AggrInfo aggrInfo,
                                                     const QString &collectionName,
                                                     qint64 totalCount, QWidget *parent) :
        BaseClass(parent),
        _textView(NULL),
        _bsonTreeview(NULL),
        _thread(NULL),
        _bsonTable(NULL),
        _isTextModeSupported(true),
        _isTreeModeSupported(true),
        _isTableModeSupported(true),
        _isCustomModeSupported(!type.isEmpty()),
        _isTextModeInitialized(false),
        _isTreeModeInitialized(false),
        _isCustomModeInitialized(false),
        _isTableModeInitialized(false),
        _isFirstPartRendered(false),
        _documents(documents),
        _queryInfo(queryInfo),
        _type(type),
        _shell(shell),
        _initialSkip(queryInfo._skip),
        _initialLimit(queryInfo._limit),
        _outputWidget(dynamic_cast<OutputWidget*>(parentWidget())),
        _mod(NULL),
        _viewMode(viewMode),
        _aggrInfo(aggrInfo),
        _collectionName(collectionName),
        _totalCount(totalCount)
    {
        setup(secs, multipleResults, tabbedResults, firstItem, lastItem);
    }

    void OutputItemContentWidget::setTotalCount(qint64 count)
    {
        _totalCount = count;
        if (!_header)
            return;

        if (count >= 0)
            _header->setCount(count);
        else
            _header->setCountState(count);
    }

    void OutputItemContentWidget::setup(double secs, bool multipleResults, bool tabbedResults,
                                       bool firstItem, bool lastItem)
    {      
        setContentsMargins(0, 0, 0, 0);
        _header = new OutputItemHeaderWidget(this, multipleResults, tabbedResults, firstItem, lastItem);

        if (_queryInfo._info.isValid()) {
            _header->setCollection(QtUtils::toQString(_queryInfo._info._ns.collectionName()));
            _header->paging()->setBatchSize(_queryInfo._batchSize);
            _header->paging()->setSkip(_queryInfo._skip);
            if (!_queryInfo._limit)
                _queryInfo._limit = 50;
        }
        else if (_aggrInfo.isValid) {
            _initialLimit = 0;
            _initialSkip = 0;
            _header->setCollection(QtUtils::toQString(_aggrInfo.collectionName));
            _header->paging()->setBatchSize(_aggrInfo.batchSize);
            _header->paging()->setSkip(_aggrInfo.skip);
        }
        else if (!_collectionName.isEmpty()) {
            // Non-cursor result (e.g. db.coll.count()) — collection name recovered
            // from the statement text by ScriptEngine.
            _header->setCollection(_collectionName);
        }

        _header->setTime(QString("%1 sec.").arg(secs, 0, 'g', 3));

        // -2 = count pending (worker computes it in the background), -3 = unavailable,
        // -1 = unknown -> fall back to the number of documents we actually hold.
        if (_totalCount == -2 || _totalCount == -3)
            _header->setCountState(_totalCount);
        else
            _header->setCount(_totalCount >= 0 ? _totalCount
                                               : static_cast<qint64>(_documents.size()));

        QVBoxLayout *layout = new QVBoxLayout();
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(_header);
        _stack = new QStackedWidget;
        layout->addWidget(_stack);
        setLayout(layout);
        configureModel();

        VERIFY(connect(_header->paging(), SIGNAL(refreshed(int, int)), this, SLOT(refresh(int, int))));
        VERIFY(connect(_header->paging(), SIGNAL(leftClicked(int, int)), this, SLOT(paging_leftClicked(int, int))));
        VERIFY(connect(_header->paging(), SIGNAL(rightClicked(int, int)), this, SLOT(paging_rightClicked(int, int))));
        VERIFY(connect(_header, SIGNAL(maximizedPart()), this, SIGNAL(maximizedPart())));
        VERIFY(connect(_header, SIGNAL(restoredSize()), this, SIGNAL(restoredSize())));
        VERIFY(connect(_header->paging(), SIGNAL(exportRequested()), this, SLOT(exportJson())));

        refreshOutputItem();
    }

    void OutputItemContentWidget::paging_leftClicked(int skip, int limit)
    {
        int s = skip - limit;

        if (s < 0)
            s = 0;

        refresh(s, limit);
    }

    void OutputItemContentWidget::exportJson()
    {
        if (!_queryInfo._info.isValid() && !_aggrInfo.isValid) {
            QMessageBox::information(this, tr("Export JSON"),
                                     tr("There is nothing to export for this result."));
            return;
        }
        if (_totalCount == 0) {
            QMessageBox::information(this, tr("Export JSON"),
                                     tr("There are no documents to export."));
            return;
        }

        // <connection>-<db>-<collection>-<yyMMddhhmm>.json
        std::string const connectionName = _shell && _shell->server()
                && _shell->server()->connectionRecord()
            ? _shell->server()->connectionRecord()->connectionName()
            : std::string("connection");

        std::string dbName;
        std::string collectionName;
        if (_queryInfo._info.isValid()) {
            dbName = _queryInfo._info._ns.databaseName();
            collectionName = _queryInfo._info._ns.collectionName();
        }
        else if (_aggrInfo.isValid) {
            dbName = _aggrInfo.dbName;
            collectionName = _aggrInfo.collectionName;
        }

        auto const sanitize = [](std::string const &value) {
            std::string result;
            result.reserve(value.size());
            for (char const c : value) {
                bool const bad = (c == '/' || c == '\\' || c == ':' || c == '*' ||
                                  c == '?' || c == '"' || c == '<' || c == '>' ||
                                  c == '|' || c == ' ');
                result += bad ? '_' : c;
            }
            return result.empty() ? std::string("unnamed") : result;
        };

        QString const defaultName = QString("%1-%2-%3-%4.json")
                .arg(QString::fromStdString(sanitize(connectionName)))
                .arg(QString::fromStdString(sanitize(dbName)))
                .arg(QString::fromStdString(sanitize(collectionName)))
                .arg(QDateTime::currentDateTime().toString("yyMMddhhmm"));

        QString const path = QFileDialog::getSaveFileName(
                this, tr("Export JSON"),
                QDir::homePath() + QLatin1Char('/') + defaultName,
                tr("JSON files (*.json)"));
        if (path.isEmpty())
            return;

        // Full result: strip pagination so the export matches the displayed
        // (total) count instead of the current page. Worker streams to disk
        // and publishes ExportProgressEvent for the status-bar indicator.
        MongoQueryInfo info(_queryInfo);
        info._limit = 0;  // unlimited
        info._skip = 0;

        AppRegistry::instance().bus()->send(
                _shell->server()->worker(),
                new ExportQueryRequest(this, info, _aggrInfo, path.toStdString(), _totalCount));
    }

    void OutputItemContentWidget::refreshOutputItem()
    {
        switch(_viewMode) {
            case Text: showText(); break;
            case Tree: showTree(); break;
            case Table: showTable(); break;
            case Custom: showCustom(); break;
            default: showTree();
        }
    }

    void OutputItemContentWidget::paging_rightClicked(int skip, int limit)
    {
        skip += limit;
        refresh(skip, limit);
    }

    void OutputItemContentWidget::refresh(int skip, int batchSize)
    {
        // Cannot set skip lower than in the text query
        if (skip <  _initialSkip) {
            _header->paging()->setSkip(_initialSkip);
            skip = _initialSkip;
        }

        int skipDelta = skip - _initialSkip;
        int limit = batchSize;

        // If limit is set to 0 it means UNLIMITED number of documents (limited only by batch size)
        // This is according to MongoDB documentation.
        if (_initialLimit != 0) {
            limit = _initialLimit - skipDelta;
            if (limit <= 0)
                limit = -1; // It means that we do not need to load documents

            if (limit > batchSize)
                limit = batchSize;
        }

        MongoQueryInfo info(_queryInfo);
        info._limit = limit;
        info._skip = skip;
        info._batchSize = batchSize;
        _outputWidget->showProgress();
                
        _shell->setScriptExecutable(true);
        if (_aggrInfo.isValid) {
            // Build original pipeline object, and append extra skip and limit for paging
            std::string pipelineModified = "[";
            for (int i = 0; ; i++) {
                auto const obj = mongo::tojson(_aggrInfo.pipeline.getObjectField(std::to_string(i)));
                if (obj.empty() || "{}" == obj)
                    break;

                pipelineModified.append(obj + ",");
            }
            pipelineModified.append("{$skip:" + std::to_string(skip) + "}, " +
                                    "{$limit:" + std::to_string(batchSize) + "}" + 
                                    "]");

            std::string const query = "db.getCollection('" + _aggrInfo.collectionName + "').aggregate(" +
                                      pipelineModified + ", " + _aggrInfo.options.toString() + ")";
            
            // Create aggr. info with new skip and batchsize
            AggrInfo aggrInfo { _aggrInfo.collectionName, skip, batchSize, _aggrInfo.pipeline, 
                                      _aggrInfo.options, _outputWidget->resultIndex(this) };
            aggrInfo.dbName = _aggrInfo.dbName;
            _shell->setAggrInfo(aggrInfo);
            _shell->execute(query);
        }
        else
            _shell->query(_outputWidget->resultIndex(this), info);
    }

    void OutputItemContentWidget::updateWithInfo(const MongoQueryInfo &inf, 
                                                 const std::vector<MongoDocumentPtr> &documents)
    {
        update(documents, inf._skip, inf._batchSize);
    }

    void OutputItemContentWidget::updateWithInfo(const AggrInfo &aggrInfo, 
                                                 const std::vector<MongoDocumentPtr> &documents)
    {
        update(documents, aggrInfo.skip, aggrInfo.batchSize);
    }

    void OutputItemContentWidget::update(const std::vector<MongoDocumentPtr> &documents, int skip, int batchSize)
    {
        _documents = documents;

        _header->paging()->setSkip(skip);
        _header->paging()->setBatchSize(batchSize);

        _text.clear();
        _isFirstPartRendered = false;
        markUninitialized();

        if (_bsonTable) {
            _stack->removeWidget(_bsonTable);
            delete _bsonTable;
            _bsonTable = NULL;
        }

        if (_bsonTreeview) {
            _stack->removeWidget(_bsonTreeview);
            delete _bsonTreeview;
            _bsonTreeview = NULL;
        }

        if (_textView) {
            _stack->removeWidget(_textView);
            delete _textView;
            _textView = NULL;
        }
        configureModel();
    }

    void OutputItemContentWidget::showText()
    {
        _viewMode = Text;
        _header->showText();
        if (!_isTextModeSupported)
            return;

        if (!_isTextModeInitialized)
        {
            _textView = configureLogText();
            if (!_text.isEmpty()) {
                _textView->sciScintilla()->setText(_text);
            }
            else {
                if (_documents.size() > 0) {
                    _textView->sciScintilla()->setText("Loading...");
                    _thread = new JsonPrepareThread(_documents, AppRegistry::instance().settingsManager()->uuidEncoding(), AppRegistry::instance().settingsManager()->timeZone());
                    VERIFY(connect(_thread, SIGNAL(partReady(const QString&)), this, SLOT(jsonPartReady(const QString&))));
                    VERIFY(connect(_thread, SIGNAL(finished()), _thread, SLOT(deleteLater())));
                    _thread->start();
                }
            }
            _stack->addWidget(_textView);
            _isTextModeInitialized = true;
        }

        _stack->setCurrentWidget(_textView);
    }

    void OutputItemContentWidget::showTree()
    {
        _viewMode = Tree;
        _header->showTree();
        if (!_isTreeModeSupported) {
            // try to downgrade to text mode
            showText();
            _viewMode = Tree;
            return;
        }

        if (!_isTreeModeInitialized) {
            _bsonTreeview = new BsonTreeView(_shell, _queryInfo, this);
            _bsonTreeview->setModel(_mod);
            _stack->addWidget(_bsonTreeview);

            if (true == AppRegistry::instance().settingsManager()->autoExpand())
                // Expanding only one level, because on large
                // documents it can take much time
                _bsonTreeview->expand(_mod->index(0, 0, QModelIndex()));

            _isTreeModeInitialized = true;
        }

        _stack->setCurrentWidget(_bsonTreeview);
    }

    void OutputItemContentWidget::showCustom()
    {
        _viewMode = Custom;
        _header->showCustom();

        if (!_isCustomModeSupported) {
            // try to downgrade to tree mode
            showTree();
            _viewMode = Custom;
            return;
        }

        if (!_isCustomModeInitialized) {

            if (_type == "collectionStats") {
                _collectionStats = new CollectionStatsTreeWidget(_documents, NULL);
                _stack->addWidget(_collectionStats);
            }               
            _isCustomModeInitialized = true;
        }

        if (_collectionStats)
            _stack->setCurrentWidget(_collectionStats);
    }

    void OutputItemContentWidget::showTable()
    {
        _viewMode = Table;
        _header->showTable();
        if (!_isTableModeSupported) {
            // try to downgrade to text mode
            showText();
            _viewMode = Table;
            return;
        }

        if (!_isTableModeInitialized) {
            _bsonTable = new BsonTableView(_shell, _queryInfo);
            BsonTableModelProxy *modp = new BsonTableModelProxy(_bsonTable);
            modp->setSourceModel(_mod);
            _bsonTable->setModel(modp);
            _stack->addWidget(_bsonTable);
            _isTableModeInitialized = true;
        }

        _stack->setCurrentWidget(_bsonTable);
    }

    void OutputItemContentWidget::markUninitialized()
    {
        _isTextModeInitialized = false;
        _isTreeModeInitialized = false;
        _isCustomModeInitialized = false;
        _isTableModeInitialized = false;
    }

    void OutputItemContentWidget::applyDockUndockSettings(bool isDocking) const
    {
        _header->applyDockUndockSettings(isDocking);
    }

    void OutputItemContentWidget::toggleOrientation(Qt::Orientation orientation) const
    {
        _header->toggleOrientation(orientation);
    }

    void OutputItemContentWidget::jsonPartReady(const QString &json)
    {
        // check that this is our current thread
        JsonPrepareThread *thread = qobject_cast<JsonPrepareThread *>(sender());
        if (thread && thread != _thread)
        {
            // close previous thread
            thread->stop();
            thread->wait();
        }
        else
        {
            if (_textView)
            {
                if (_isFirstPartRendered)
                    _textView->sciScintilla()->append(json);
                else
                    _textView->sciScintilla()->setText(json);
                _isFirstPartRendered = true;
            }
        }
    }
    
    BsonTreeModel *OutputItemContentWidget::configureModel()
    {
        delete _mod;
        _mod = new BsonTreeModel(_documents, this);
        return _mod;
    }

    FindFrame *Robomongo::OutputItemContentWidget::configureLogText()
    {
        const QFont &textFont = GuiRegistry::instance().font();

        QsciLexerJavaScript *javaScriptLexer = new JSLexer(this);
        javaScriptLexer->setFont(textFont);

        FindFrame *_logText = new FindFrame(this);
        _logText->sciScintilla()->setLexer(javaScriptLexer);
        _logText->sciScintilla()->setTabWidth(4);        
        _logText->sciScintilla()->setAppropriateBraceMatching();
        _logText->sciScintilla()->setFont(textFont);
        _logText->sciScintilla()->setReadOnly(true);
        _logText->sciScintilla()->setWrapMode((QsciScintilla::WrapMode) QsciScintilla::SC_WRAP_NONE);
        _logText->sciScintilla()->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        _logText->sciScintilla()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // Wrap mode turned off because it introduces huge performance problems
        // even for medium size documents.    
        _logText->sciScintilla()->setStyleSheet("QFrame {background-color: rgb(73, 76, 78); border: 1px solid #c7c5c4; border-radius: 0px; margin: 0px; padding: 0px;}");
        return _logText;
    }
}
