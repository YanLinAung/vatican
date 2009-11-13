#include "vatican.h"
#include <cstdlib>  // for abort
#include <ostream>
#include <set>

namespace vatican {

enum UplinkType { UPLINK_APPL, UPLINK_APPR, UPLINK_NA };

struct Uplink {
    Node* link;
    UplinkType type;
    Uplink* prev;
    Uplink* next;
};

struct UplinkSet {
    UplinkSet() {
        head = 0;
        tail = 0;
    }

    Uplink* head;
    Uplink* tail;

    Uplink* add(Node* link, UplinkType type) {
        Uplink* up = new Uplink;
        up->link = link;
        up->type = type;
        up->prev = tail;
        up->next = 0;
        if (tail) { tail->next = up; }
        tail = up;
        if (head == 0) head = up;
        return up;
    }

    void remove(Uplink* link) {
        if (link->prev) { 
            link->prev->next = link->next; 
        }
        else {
            head = link->next;
        }
        
        if (link->next) { 
            link->next->prev = link->prev;
        }
        else {
            tail = link->prev;
        }

        delete link;
    }

    void unlink(Node* parent, UplinkType type) {
        Uplink* cur = head;
        while (cur) {
            if (cur->link == parent && cur->type == type) {
                remove(cur);
                return;
            }
            cur = cur->next;
        }
        std::abort();
    }
};

struct AppNode {
    Node* left;
    Node* right;
};

struct LambdaNode {
    Node* body;
    Node* var;
};

struct VarNode { };

enum NodeType { NODE_APP, NODE_LAMBDA, NODE_VAR, NODE_PRIM };

class Node {
  public:
    Node* cache;
    UplinkSet uplinks;
    NodeType type;
    union {
        AppNode app;
        LambdaNode lambda;
        VarNode var;
        PrimNode* prim;
    };
};

class Head {
  public:
    Node* dummy;
};

static void upcopy(Node* newchild, Node* into, UplinkType type) {
    Node* newNode;

    switch (into->type) {
        case NODE_APP: {
            if (into->cache == 0) {
                newNode = new Node;
                newNode->type = NODE_APP;
                newNode->cache = 0;
                // don't install uplinks when creating app nodes,
                // they will be created on the clear pass
                if (type == UPLINK_APPL) {
                    newNode->app.left = newchild;
                    newNode->app.right = into->app.right;
                }
                else {
                    newNode->app.left = into->app.left;
                    newNode->app.right = newchild;
                }
            }
            else {
                newNode = into->cache;
                if (type == UPLINK_APPL) {
                    newNode->app.left = newchild;
                }
                else {
                    newNode->app.right = newchild;
                }
                return;  // don't traverse!
            }

            into->cache = newNode;
            break;
        }
        case NODE_LAMBDA: {
            if (into->cache == (Node*)~0) { // special terminal condition
                return; 
            } 

            // allocate a fresh variable node for the new lambda 
            Node* newVar = new Node;
            newVar->type = NODE_VAR;
            newVar->var = VarNode();
            newVar->cache = 0;
            
            // prepare the new lambda node
            newNode = new Node;
            newNode->type = NODE_LAMBDA;
            newNode->lambda.body = newchild;
            newNode->lambda.var = newVar;
            newNode->cache = 0;

            into->cache = newNode;

            // replace occurrences of the old variable with the new one
            // (tricky business, see paper)
            upcopy(newVar, into->lambda.var, UPLINK_NA);
            break;
        }
        case NODE_VAR: {
            newNode = newchild;
            into->cache = newNode;
            break;
        }
        case NODE_PRIM: {
            newNode = newchild;
            into->cache = newNode;
        }
        default: std::abort();
    }
    
    Uplink* cur = into->uplinks.head;
    while (cur) {
        upcopy(newNode, cur->link, cur->type);
        cur = cur->next;
    }
}

static void clear(Node* node) {
    Uplink* cur = node->uplinks.head;
    while (cur) {
        if (cur->link->cache == 0) {
            cur = cur->next;
            continue;
        }

        // install uplinks, since we omitted them above
        switch (cur->link->type) {
            case NODE_APP: {
                cur->link->cache->app.left->uplinks.add(cur->link->cache, UPLINK_APPL);
                cur->link->cache->app.right->uplinks.add(cur->link->cache, UPLINK_APPR);
                cur->link->cache = 0;
                break;
            }
            case NODE_LAMBDA: {
                cur->link->cache->lambda.body->uplinks.add(cur->link->cache, UPLINK_NA);
                cur->link->cache = 0;
                clear(cur->link->lambda.var);
                break;
            }
            default: std::abort();
        }

        clear(cur->link);
        cur = cur->next;
    }
}

static void cleanup(Node* node) {
    if (node->uplinks.head != 0) return;

    switch (node->type) {
        case NODE_LAMBDA: {
            node->lambda.body->uplinks.unlink(node, UPLINK_NA);
            cleanup(node->lambda.body);
            delete node;
            break;
        }
        case NODE_APP: {
            node->app.left->uplinks.unlink(node, UPLINK_APPL);
            cleanup(node->app.left);
            node->app.right->uplinks.unlink(node, UPLINK_APPR);
            cleanup(node->app.right);
            delete node;
            break;
        }
        case NODE_VAR: {
            delete node;
            break;
        }
        case NODE_PRIM: {
            delete node->prim;
            delete node;
            break;
        }
        default: std::abort();
    }
}

static void upreplace(Node* newchild, Node* into, UplinkType type) {
    switch (into->type) {
        case NODE_APP: {
            if (type == UPLINK_APPL) { 
                Node* old = into->app.left;
                old->uplinks.unlink(into, UPLINK_APPL);
                into->app.left = newchild;
                newchild->uplinks.add(into, UPLINK_APPL);
                cleanup(old);
            }
            else {
                Node* old = into->app.right;
                old->uplinks.unlink(into, UPLINK_APPR);
                into->app.right = newchild;
                newchild->uplinks.add(into, UPLINK_APPR);
                cleanup(old);
            }
            break;
        }
        case NODE_LAMBDA: {
            Node* old = into->lambda.body;
            old->uplinks.unlink(into, UPLINK_NA);
            into->lambda.body = newchild;
            newchild->uplinks.add(into, UPLINK_NA);
            cleanup(old);
            break;
        }
        default: std::abort();
    }
}

static void beta_reduce(Node* app) {
    Node* fun = app->app.left;
    Node* arg = app->app.right;
    
    Node* result;
    if (fun->lambda.var->uplinks.head == 0) {
        result = fun->lambda.body;
    }
    else {
        fun->cache = (Node*)~0x0;  // special code for where to stop
        
        upcopy(arg, fun->lambda.var, UPLINK_NA);

        result = fun->lambda.body->cache;
        fun->cache = 0;
        clear(fun->lambda.var);
    }

    Uplink* cur = app->uplinks.head;
    while (cur) {
        Uplink* nexty = cur->next;
        upreplace(result, cur->link, cur->type);
        cur = nexty;
    }
}

static bool prim_reduce(Node* app) {
    Node* fun = app->app.left;
    Node* arg = app->app.right;

    Head* arghead = make_head(arg);
    PrimNode* p = fun->prim->apply(arghead);
    free_head(arghead);

    if (p == 0) {
        // didn't reduce
        return false;
    }
    else {
        Node* result = new Node;
        result->cache = 0;
        result->type = NODE_PRIM;
        result->prim = p;

        Uplink* cur = app->uplinks.head;
        while (cur) {
            Uplink* nexty = cur->next;
            upreplace(result, cur->link, cur->type);
            cur = nexty;
        }
        return true;
    }


}

static bool hnf_reduce_1(Node* ptr) {
    switch (ptr->type) {
        case NODE_LAMBDA: {
            return hnf_reduce_1(ptr->lambda.body);
        }
        case NODE_APP: {
            bool reduced = hnf_reduce_1(ptr->app.left);
            if (reduced) { return true; }

            if (ptr->app.left->type == NODE_LAMBDA) {
                beta_reduce(ptr);
                return true;
            }
            else if (ptr->app.left->type == NODE_PRIM) {
                return prim_reduce(ptr);
            }
            else {
                return false;
            }
        }
        case NODE_VAR: {
            return false;
        }
        case NODE_PRIM: {
            return false;
        }
        default: std::abort();
    }
}

bool hnf_reduce_1(Head* ptr) {
    return hnf_reduce_1(ptr->dummy);
}

void hnf_reduce(Head* top) {
    while (hnf_reduce_1(top)) { }
}

static void dotify_rec(Node* top, std::ostream& stream, std::set<Node*>* seen) {
    if (seen->find(top) != seen->end()) return;
    seen->insert(top);

    switch (top->type) {
        case NODE_LAMBDA: {
            stream << "p" << top << " [label=\"\\\\\"];\n";
            stream << "p" << top << " -> p" << top->lambda.body << ";\n";
            if (top->lambda.var->uplinks.head != 0) {
                stream << "p" << top << " -> p" << top->lambda.var << " [color=blue];\n";
            }
            dotify_rec(top->lambda.body, stream, seen);
            break;
        }
        case NODE_APP: {
            stream << "p" << top << " [label=\"*\"];\n";
            stream << "p" << top << " -> p" << top->app.left << " [color=\"#007f00\",label=\"fv\"];\n";
            stream << "p" << top << " -> p" << top->app.right << " [label=\"av\"];\n";
            dotify_rec(top->app.left, stream, seen);
            dotify_rec(top->app.right, stream, seen);
            break;
        }
        case NODE_VAR: {
            stream << "p" << top << " [label=\"x\"];\n";
            break;
        }
        case NODE_PRIM: {
            stream << "p" << top << " [label=\"" << top->prim->repr() << "\"];\n";
            break;
        }
        default: std::abort();
    }

    Uplink* cur = top->uplinks.head;
    while (cur) {
        stream << "p" << top << " -> p" << cur->link << " [color=red];\n";
        cur = cur->next;
    }
}

void dotify(Head* top, std::ostream& stream) {
    std::set<Node*> set;
    stream << "digraph Lambda {\n";
    stream << "p" << top->dummy << " [label=\"HEAD\"];\n";
    stream << "p" << top->dummy << " -> p" << top->dummy->lambda.body << ";\n";
    set.insert(top->dummy);
    dotify_rec(top->dummy->lambda.body, stream, &set);
    stream << "}\n";
}

Head* make_head(Node* body) {
    Head* ret = new Head;
    ret->dummy = Fun(Var(), body);
    return ret;
}

Head* copy_head(Head* other) {
    Head* ret = new Head;
    ret->dummy = Fun(Var(), other->dummy);
    return ret;
}

void free_head(Head* head) {
    cleanup(head->dummy);
    delete head;
}

Node* Var() {
    Node* ret = new Node;
    ret->type = NODE_VAR;
    ret->cache = 0;
    return ret;
}

Node* Fun(Node* var, Node* body) {
    Node* ret = new Node;
    ret->type = NODE_LAMBDA;
    ret->lambda.body = body;
    ret->lambda.var = var;
    ret->cache = 0;
    body->uplinks.add(ret, UPLINK_NA);
    return ret;
}

Node* App(Node* left, Node* right) {
    Node* ret = new Node;
    ret->type = NODE_APP;
    ret->app.left = left;
    ret->app.right = right;
    ret->cache = 0;
    left->uplinks.add(ret, UPLINK_APPL);
    right->uplinks.add(ret, UPLINK_APPR);
    return ret;
}

Node* Prim(PrimNode* node) {
    Node* r = new Node;
    r->type = NODE_PRIM;
    r->cache = 0;
    r->prim = node;
    return r;
}

PrimNode* get_prim(Head* expr) {
    if (expr->dummy->type != NODE_LAMBDA || expr->dummy->lambda.body->type != NODE_PRIM) {
        return 0;
    }
    return expr->dummy->lambda.body->prim;
}

}
